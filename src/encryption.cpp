#include "encryption.h"
#include "compression.h"
#include "io_utils.h"
#include "png_utils.h"

#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {
struct TermiosGuard {
	termios old{};
	sigset_t guarded_signals{};
	sigset_t old_signal_mask{};
	int signal_fd{-1};
	bool terminal_active{false};
	bool signals_blocked{false};

	TermiosGuard(const TermiosGuard&) = delete;
	TermiosGuard& operator=(const TermiosGuard&) = delete;

	[[nodiscard]] static int setTerminalAttributes(const termios& attributes) noexcept {
		int rc;
		do {
			rc = tcsetattr(STDIN_FILENO, TCSAFLUSH, &attributes);
		} while (rc != 0 && errno == EINTR);
		return rc;
	}

	void closeSignalFd() noexcept {
		if (signal_fd < 0) return;
		::close(signal_fd);
		signal_fd = -1;
	}

	void restoreNoThrow() noexcept {
		// Restore the terminal before unblocking signals: a pending terminating
		// signal must never be able to strand the terminal in noncanonical/no-echo
		// mode.
		if (terminal_active && setTerminalAttributes(old) == 0) {
			terminal_active = false;
		}
		closeSignalFd();
		if (signals_blocked && sigprocmask(SIG_SETMASK, &old_signal_mask, nullptr) == 0) {
			signals_blocked = false;
		}
	}

	explicit TermiosGuard(bool is_tty) {
		if (!is_tty) {
			return;
		}

		if (sigemptyset(&guarded_signals) != 0 ||
			sigaddset(&guarded_signals, SIGINT) != 0 ||
			sigaddset(&guarded_signals, SIGTERM) != 0 ||
			sigaddset(&guarded_signals, SIGHUP) != 0 ||
			sigaddset(&guarded_signals, SIGQUIT) != 0 ||
			sigaddset(&guarded_signals, SIGTSTP) != 0) {
			throw std::runtime_error("Terminal Error: Unable to prepare signal protection for PIN entry.");
		}
		if (sigprocmask(SIG_BLOCK, &guarded_signals, &old_signal_mask) != 0) {
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to protect PIN entry from signals: {}", ec.message()));
		}
		signals_blocked = true;

		signal_fd = signalfd(-1, &guarded_signals, SFD_CLOEXEC | SFD_NONBLOCK);
		if (signal_fd < 0) {
			const std::error_code ec(errno, std::generic_category());
			restoreNoThrow();
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to monitor signals during PIN entry: {}", ec.message()));
		}

		if (tcgetattr(STDIN_FILENO, &old) != 0) {
			const std::error_code ec(errno, std::generic_category());
			restoreNoThrow();
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to read terminal settings for secure PIN entry: {}", ec.message()));
		}
		termios newt = old;
		const auto mask = static_cast<tcflag_t>(ICANON | ECHO);
		newt.c_lflag &= static_cast<tcflag_t>(~mask);
		if (setTerminalAttributes(newt) != 0) {
			const std::error_code ec(errno, std::generic_category());
			restoreNoThrow();
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to disable terminal echo for secure PIN entry: {}", ec.message()));
		}
		terminal_active = true;
	}

	~TermiosGuard() {
		restoreNoThrow();
	}

	[[nodiscard]] bool masksInput() const noexcept {
		return terminal_active;
	}

	struct ReadResult {
		ssize_t count{};
		int signal_number{};
	};

	[[nodiscard]] ReadResult readByte(char& ch) const noexcept {
		if (!terminal_active) {
			return ReadResult{ .count = ::read(STDIN_FILENO, &ch, 1) };
		}

		std::array<pollfd, 2> fds{{
			pollfd{ .fd = signal_fd, .events = POLLIN, .revents = 0 },
			pollfd{ .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 }
		}};

		while (true) {
			const int poll_result = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), -1);
			if (poll_result < 0) {
				if (errno == EINTR) continue;
				return ReadResult{ .count = -1 };
			}

			if ((fds[0].revents & POLLIN) != 0) {
				signalfd_siginfo info{};
				const ssize_t signal_bytes = ::read(signal_fd, &info, sizeof(info));
				if (signal_bytes == static_cast<ssize_t>(sizeof(info))) {
					return ReadResult{
						.count = 0,
						.signal_number = static_cast<int>(info.ssi_signo)
					};
				}
				if (signal_bytes < 0 && errno == EINTR) continue;
				if (signal_bytes < 0 && errno == EAGAIN) continue;
				return ReadResult{ .count = -1 };
			}

			if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
				errno = EIO;
				return ReadResult{ .count = -1 };
			}
			if ((fds[1].revents & (POLLIN | POLLHUP)) != 0) {
				return ReadResult{ .count = ::read(STDIN_FILENO, &ch, 1) };
			}
			if ((fds[1].revents & (POLLERR | POLLNVAL)) != 0) {
				errno = EIO;
				return ReadResult{ .count = -1 };
			}
		}
	}

	void finish() {
		int terminal_error = 0;
		int signal_error = 0;

		if (terminal_active) {
			if (setTerminalAttributes(old) == 0) {
				terminal_active = false;
			} else {
				terminal_error = errno;
			}
		}
		closeSignalFd();
		if (signals_blocked) {
			if (sigprocmask(SIG_SETMASK, &old_signal_mask, nullptr) == 0) {
				signals_blocked = false;
			} else {
				signal_error = errno;
			}
		}

		if (terminal_error != 0) {
			const std::error_code ec(terminal_error, std::generic_category());
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to restore terminal settings after PIN entry: {}", ec.message()));
		}
		if (signal_error != 0) {
			const std::error_code ec(signal_error, std::generic_category());
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to restore the signal mask after PIN entry: {}", ec.message()));
		}
	}

	void forwardSignal(int signal_number) {
		// Use the checked restoration path. If restoration itself fails, report that
		// error and let the destructor retry instead of terminating with a damaged
		// terminal configuration.
		finish();
		// The signal was consumed from signalfd.  Re-deliver it only after the
		// terminal and original signal mask have been restored.  With the normal
		// disposition this does not return; if a caller-supplied handler returns
		// (or the signal was originally blocked), fail the PIN operation.
		if (::raise(signal_number) != 0) {
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format(
				"PIN Error: Unable to re-deliver interrupt signal: {}", ec.message()));
		}
		throw std::runtime_error("PIN Error: PIN entry interrupted by signal.");
	}
};

constexpr std::size_t STREAM_CHUNK_SIZE = 1 * 1024 * 1024;

using StreamHeader = std::array<Byte, crypto_secretstream_xchacha20poly1305_HEADERBYTES>;

struct KdfSecrets {
	Salt salt{};
	StreamHeader stream_header{};
};

static_assert(STREAM_FRAME_LEN_BYTES == sizeof(std::uint32_t),
	"The frame length is written by updateValue(), which encodes exactly 32 bits.");

[[nodiscard]] std::array<Byte, STREAM_FRAME_LEN_BYTES> encodeFrameLen(std::uint32_t frame_len) {
	std::array<Byte, STREAM_FRAME_LEN_BYTES> bytes{};
	updateValue(bytes, 0, frame_len);
	return bytes;
}

void appendEncryptedFrame(
	vBytes& encrypted,
	std::span<const Byte> cipher_frame,
	std::size_t max_encrypted_size) {
	if (cipher_frame.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
		throw std::runtime_error("crypto_secretstream frame too large.");
	}

	const auto frame_len = encodeFrameLen(static_cast<std::uint32_t>(cipher_frame.size()));
	const std::size_t base = encrypted.size();
	const std::size_t final_size = checkedAddSize(
		checkedAddSize(base, frame_len.size(), "File Size Error: Encrypted output overflow."),
		cipher_frame.size(),
		"File Size Error: Encrypted output overflow."
	);
	if (final_size > max_encrypted_size) {
		throw std::runtime_error(
			"File Size Error: Compressed and encrypted payload exceeds the selected output size limit.");
	}

	encrypted.resize(final_size);
	Byte* out = encrypted.data() + static_cast<std::ptrdiff_t>(base);
	std::memcpy(out, frame_len.data(), frame_len.size());
	std::memcpy(out + frame_len.size(), cipher_frame.data(), cipher_frame.size());
}

void initializeSecretStreamPush(
	crypto_secretstream_xchacha20poly1305_state& state,
	StreamHeader& stream_header,
	const Key& key) {

	if (crypto_secretstream_xchacha20poly1305_init_push(&state, stream_header.data(), key.data()) != 0) {
		throw std::runtime_error("crypto_secretstream init_push failed.");
	}
}

void appendEncryptedFrames(
	vBytes& encrypted,
	std::span<const Byte> plaintext,
	crypto_secretstream_xchacha20poly1305_state& state,
	vBytes& cipher_chunk,
	std::size_t max_encrypted_size,
	unsigned char final_tag = 0) {

	if (plaintext.empty() && final_tag == 0) {
		return;
	}

	std::size_t offset = 0;
	bool emit_empty_final = plaintext.empty() && final_tag != 0;

	while (emit_empty_final || offset < plaintext.size()) {
		const std::size_t remaining = plaintext.size() - offset;
		const std::size_t chunk_len = std::min(remaining, STREAM_CHUNK_SIZE);
		const bool is_final = emit_empty_final || (offset + chunk_len == plaintext.size());
		const unsigned char tag = is_final ? final_tag : 0;

		unsigned long long clen_ull = 0;
		if (crypto_secretstream_xchacha20poly1305_push(
				&state,
				cipher_chunk.data(),
				&clen_ull,
				(chunk_len == 0) ? nullptr : (plaintext.data() + static_cast<std::ptrdiff_t>(offset)),
				chunk_len,
				nullptr,
				0,
				tag) != 0) {
			throw std::runtime_error("crypto_secretstream push failed.");
		}

		if (clen_ull > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
			throw std::runtime_error("crypto_secretstream frame too large.");
		}
		appendEncryptedFrame(
			encrypted,
			std::span<const Byte>(cipher_chunk.data(), static_cast<std::size_t>(clen_ull)),
			max_encrypted_size
		);

		offset += chunk_len;
		emit_empty_final = false;
	}
}

[[nodiscard]] std::uint32_t readFrameLen(std::span<const Byte> data, std::size_t index) {
	return getValue(data, index);
}

// Decrypt in place: cipher at storage[cipher_start..cipher_start+cipher_len),
// plaintext written to storage[0..]. Plaintext write trails unread cipher by at least
// (cipher_start + header.size() + 21*N) bytes after frame N — always safe.
// On success, storage is resized to plaintext size. On failure, any partial plaintext is wiped.
[[nodiscard]] bool decryptWithSecretStreamInPlace(
	vBytes& storage,
	std::size_t cipher_start,
	std::size_t cipher_len,
	const Key& key,
	const StreamHeader& header) {

	if (cipher_len < header.size()) {
		return false;
	}
	if (!spanHasRange(storage, cipher_start, cipher_len)) {
		return false;
	}
	if (!std::ranges::equal(
			std::span<const Byte>(storage.data() + cipher_start, header.size()),
			header)) {
		return false;
	}

	crypto_secretstream_xchacha20poly1305_state stream_state{};
	ScopedWipe stream_state_wipe{stream_state};
	if (crypto_secretstream_xchacha20poly1305_init_pull(&stream_state, header.data(), key.data()) != 0) {
		return false;
	}

	vBytes plain_chunk(STREAM_CHUNK_SIZE);
	ScopedWipe plain_chunk_wipe{plain_chunk};

	std::size_t cipher_pos = cipher_start + header.size();
	const std::size_t cipher_end = cipher_start + cipher_len;
	std::size_t plain_pos = 0;
	bool has_final_tag = false;

	auto fail = [&]() -> bool {
		sodium_memzero(storage.data(), plain_pos);
		return false;
	};

	while (cipher_pos < cipher_end) {
		if (cipher_end - cipher_pos < STREAM_FRAME_LEN_BYTES) {
			return fail();
		}

		const std::uint32_t frame_len = readFrameLen(storage, cipher_pos);
		cipher_pos += STREAM_FRAME_LEN_BYTES;

		if (frame_len < crypto_secretstream_xchacha20poly1305_ABYTES ||
			frame_len > cipher_end - cipher_pos) {
			return fail();
		}

		const std::size_t max_plain_chunk = static_cast<std::size_t>(frame_len) - crypto_secretstream_xchacha20poly1305_ABYTES;
		if (max_plain_chunk > plain_chunk.size()) {
			return fail();
		}

		unsigned long long mlen = 0;
		unsigned char tag = 0;
		if (crypto_secretstream_xchacha20poly1305_pull(
				&stream_state,
				plain_chunk.data(),
				&mlen,
				&tag,
				storage.data() + cipher_pos,
				frame_len,
				nullptr,
				0) != 0) {
			return fail();
		}
		if (mlen > max_plain_chunk) {
			return fail();
		}
		if (mlen > 0) {
			std::memcpy(storage.data() + plain_pos, plain_chunk.data(), mlen);
			plain_pos += mlen;
		}

		cipher_pos += frame_len;
		if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
			has_final_tag = true;
			break;
		}
	}

	if (!has_final_tag || cipher_pos != cipher_end) {
		return fail();
	}

	// Wipe trailing ciphertext bytes before truncating.
	if (plain_pos < storage.size()) {
		sodium_memzero(storage.data() + plain_pos, storage.size() - plain_pos);
	}
	storage.resize(plain_pos);
	return true;
}

// Takes the caller's SensitiveU64 by reference rather than the integer by
// value. A by-value parameter made a second copy of the secret that this
// function then had to scrub -- and scrubbing a parameter only reaches its stack
// slot, never a register the compiler chose to keep it in. Borrowing means there
// is no second copy to begin with, and the one home the secret does have is
// wiped by its owner on scope exit. Only the decimal rendering below is
// transient here, and that is scrubbed on every path out.
void deriveKeyFromPin(Key& out_key, const SensitiveU64& pin, const Salt& salt) {
	std::array<char, 32> pin_buf{};
	ScopedWipe pin_buf_wipe{pin_buf};

	auto [ptr, ec] = std::to_chars(pin_buf.data(), pin_buf.data() + pin_buf.size(), pin.value);
	if (ec != std::errc{}) {
		throw std::runtime_error("KDF Error: Failed to encode recovery PIN.");
	}

	const auto pin_len = static_cast<unsigned long long>(ptr - pin_buf.data());
	const int rc = crypto_pwhash(
		out_key.data(),
		out_key.size(),
		pin_buf.data(),
		pin_len,
		salt.data(),
		crypto_pwhash_OPSLIMIT_INTERACTIVE,
		crypto_pwhash_MEMLIMIT_INTERACTIVE,
		crypto_pwhash_ALG_ARGON2ID13
	);

	if (rc != 0) {
		throw std::runtime_error("KDF Error: Unable to derive encryption key.");
	}
}

// Fills `out_pin` in place; see SensitiveU64 for why no secret is returned by
// value anywhere in this file.
void generateRecoveryPin(SensitiveU64& out_pin) {
	out_pin.value = 0;
	while (out_pin.value == 0) {
		randombytes_buf(&out_pin.value, sizeof(out_pin.value));
	}
}

inline constexpr std::size_t MAX_FILENAME_PREFIX_BYTES =
	static_cast<std::size_t>(std::numeric_limits<Byte>::max()) + 1;

struct FilenamePrefix {
	std::array<Byte, MAX_FILENAME_PREFIX_BYTES> bytes{};
	std::size_t size{};

	[[nodiscard]] std::span<const Byte> view() const noexcept {
		return std::span<const Byte>(bytes.data(), size);
	}
};

[[nodiscard]] FilenamePrefix makeFilenamePrefix(const std::string& data_filename) {
	if (data_filename.empty() || data_filename.size() > static_cast<std::size_t>(std::numeric_limits<Byte>::max())) {
		throw std::runtime_error("Data File Error: Invalid data filename length.");
	}

	FilenamePrefix filename_prefix;
	filename_prefix.size = 1 + data_filename.size();
	filename_prefix.bytes[0] = static_cast<Byte>(data_filename.size());
	std::memcpy(
		filename_prefix.bytes.data() + 1,
		data_filename.data(),
		data_filename.size()
	);
	return filename_prefix;
}

void writeKdfMetadata(
	vBytes& profile_vec,
	const ProfileOffsets& offsets,
	const Salt& salt,
	const StreamHeader& stream_header,
	std::string_view corrupt_profile_error) {

	// The whole region is randomised first, so the bytes around the fields below
	// stay indistinguishable padding.
	Byte* const region = profile_vec.data() + offsets.kdf_metadata;
	randombytes_buf(region, KDF_METADATA_REGION_BYTES);
	std::memcpy(region + KDF_MAGIC_OFFSET, KDF_METADATA_MAGIC_V2.data(), KDF_METADATA_MAGIC_V2.size());
	region[KDF_ALG_OFFSET] = KDF_ALG_ARGON2ID13;
	region[KDF_SENTINEL_OFFSET] = KDF_SENTINEL;

	requireSpanRange(profile_vec, offsets.kdf_metadata + KDF_SALT_OFFSET, salt.size(), corrupt_profile_error);
	requireSpanRange(profile_vec, offsets.kdf_metadata + KDF_NONCE_OFFSET, stream_header.size(), corrupt_profile_error);
	std::memcpy(region + KDF_SALT_OFFSET, salt.data(), salt.size());
	std::memcpy(region + KDF_NONCE_OFFSET, stream_header.data(), stream_header.size());
}

[[nodiscard]] KdfSecrets readKdfSecrets(
	std::span<const Byte> data,
	const ProfileOffsets& offsets,
	std::string_view corrupt_file_error) {

	KdfSecrets secrets;
	requireSpanRange(data, offsets.kdf_metadata + KDF_SALT_OFFSET, secrets.salt.size(), corrupt_file_error);
	requireSpanRange(data, offsets.kdf_metadata + KDF_NONCE_OFFSET, secrets.stream_header.size(), corrupt_file_error);

	const Byte* const region = data.data() + offsets.kdf_metadata;
	std::memcpy(secrets.salt.data(), region + KDF_SALT_OFFSET, secrets.salt.size());
	std::memcpy(secrets.stream_header.data(), region + KDF_NONCE_OFFSET, secrets.stream_header.size());
	return secrets;
}

[[nodiscard]] std::string extractFilenamePrefix(vBytes& payload) {
	constexpr const char* CORRUPT_FILE_ERROR = "File Recovery Error: Embedded profile is corrupt.";
	if (payload.empty()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	const std::size_t filename_len = payload[0];
	if (filename_len == 0) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}
	const std::size_t prefix_len = 1 + filename_len;
	requireSpanRange(payload, 0, prefix_len, CORRUPT_FILE_ERROR);

	std::string decrypted_filename(
		reinterpret_cast<const char*>(payload.data() + 1),
		filename_len
	);

	const std::size_t old_payload_size = payload.size();
	const std::size_t compressed_payload_size = old_payload_size - prefix_len;
	if (compressed_payload_size > 0) {
		std::memmove(
			payload.data(),
			payload.data() + static_cast<std::ptrdiff_t>(prefix_len),
			compressed_payload_size
		);
		sodium_memzero(
			payload.data() + static_cast<std::ptrdiff_t>(compressed_payload_size),
			prefix_len
		);
	} else {
		sodium_memzero(payload.data(), old_payload_size);
	}
	payload.resize(compressed_payload_size);
	return decrypted_filename;
}

} // namespace

void getPin(SensitiveU64& out_pin) {
	constexpr auto MAX_UINT64_STR = std::string_view{"18446744073709551615"};
	constexpr std::size_t MAX_PIN_LENGTH = 20;

	const bool is_tty = (isatty(STDIN_FILENO) != 0);
	TermiosGuard termios_guard{is_tty};

	std::print("\nPIN: ");
	if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
		throw std::runtime_error("PIN Error: Failed to display the recovery PIN prompt.");
	}

	std::array<char, MAX_PIN_LENGTH> input{};
	ScopedWipe input_wiper{input};

	std::size_t input_len = 0;
	char ch{};
	// Sticky: excess digits are discarded and cannot be restored by backspace.
	bool input_overflow = false;
	bool invalid_input = false;
	bool read_error = false;

	auto wipe_input = [&]() {
		sodium_memzero(input.data(), input.size());
		input_len = 0;
	};

	while (true) {
		const auto read_result = termios_guard.readByte(ch);
		if (read_result.signal_number != 0) {
			wipe_input();
			termios_guard.forwardSignal(read_result.signal_number);
		}
		const ssize_t bytes_read = read_result.count;
		if (bytes_read == 0) break;
		if (bytes_read < 0) {
			if (errno == EINTR) continue;
			read_error = true;
			break;
		}
		if (ch == '\n' || ch == '\r') break;
		if (ch >= '0' && ch <= '9') {
			if (input_len >= input.size()) {
				input_overflow = true;
				continue;
			}
			input[input_len++] = ch;
			if (termios_guard.masksInput()) {
				std::print("*");
				if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
					wipe_input();
					throw std::runtime_error("PIN Error: Failed to display masked PIN input.");
				}
			}
		} else if ((ch == '\b' || ch == 127) && input_len != 0) {
			if (termios_guard.masksInput()) {
				std::print("\b \b");
				if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
					wipe_input();
					throw std::runtime_error("PIN Error: Failed to display masked PIN input.");
				}
			}
			input[--input_len] = '\0';
			// Keep input_overflow set if excess digits were dropped earlier.
		} else if (ch == '\b' || ch == 127) {
			// Ignore backspace/delete with empty buffer.
		} else {
			invalid_input = true;
		}
	}

	std::println("");
	if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
		wipe_input();
		throw std::runtime_error("PIN Error: Failed to display masked PIN input.");
	}
	termios_guard.finish();

	auto failFormat = [&](std::string_view message) {
		wipe_input();
		throw std::runtime_error(std::string(message));
	};

	if (read_error) {
		failFormat("PIN Error: Failed to read recovery PIN.");
	}
	// Non-digits are never stored, so check invalid_input before empty.
	if (invalid_input) {
		failFormat("PIN Error: Recovery PIN must contain only digits.");
	}

	const std::string_view input_view(input.data(), input_len);
	if (input_view.empty()) {
		failFormat("PIN Error: Recovery PIN is required.");
	}
	if (input_overflow ||
		(input_view.length() == MAX_PIN_LENGTH && input_view > MAX_UINT64_STR)) {
		failFormat("PIN Error: Recovery PIN is too long or out of range.");
	}

	auto [ptr, ec] = std::from_chars(input.data(), input.data() + input_len, out_pin.value);
	// PIN 0 is never issued by generateRecoveryPin(); treat it as a format error
	// so we do not burn an Argon2 attempt on a known-invalid value.
	if (ec != std::errc{} || ptr != input.data() + input_len || out_pin.value == 0) {
		failFormat("PIN Error: Invalid recovery PIN format.");
	}

	wipe_input();
}

namespace {

} // namespace

namespace {

template <typename PayloadProducer>
void encryptPayloadToProfile(
	SensitiveU64& out_pin,
	vBytes& profile_vec,
	const std::string& data_filename,
	bool has_mastodon_option,
	std::size_t max_profile_size,
	PayloadProducer produce_payload) {

	const auto& offsets = has_mastodon_option ? MASTODON_OFFSETS : DEFAULT_OFFSETS;
	constexpr const char* CORRUPT_PROFILE_ERROR = "Internal Error: Corrupt profile template.";

	requireSpanRange(profile_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_PROFILE_ERROR);
	if (offsets.encrypted_file != profile_vec.size()) {
		throw std::runtime_error(CORRUPT_PROFILE_ERROR);
	}
	if (profile_vec.size() > max_profile_size) {
		throw std::runtime_error(
			"File Size Error: Cover image leaves no room for an embedded payload.");
	}

	const FilenamePrefix filename_prefix = makeFilenamePrefix(data_filename);

	Key key{};
	ScopedWipe key_wipe{key};
	Salt salt{};
	StreamHeader stream_header{};
	// Generated straight into the caller's storage: there is never a second copy
	// to scrub, and the caller's SensitiveU64 wipes it on every path out of here,
	// exception or not.
	generateRecoveryPin(out_pin);

	randombytes_buf(salt.data(), salt.size());
	deriveKeyFromPin(key, out_pin, salt);

	crypto_secretstream_xchacha20poly1305_state stream_state{};
	ScopedWipe stream_state_wipe{stream_state};
	initializeSecretStreamPush(stream_state, stream_header, key);

	if (stream_header.size() > max_profile_size - profile_vec.size()) {
		throw std::runtime_error(
			"File Size Error: Compressed and encrypted payload exceeds the selected output size limit.");
	}
	appendBytes(profile_vec, std::span<const Byte>(stream_header), "File Size Error: Encrypted output overflow.");

	vBytes cipher_chunk(STREAM_CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES);

	appendEncryptedFrames(
		profile_vec, filename_prefix.view(), stream_state, cipher_chunk, max_profile_size);

	bool saw_compressed_output = false;
	const auto consume_payload = [&](std::span<const Byte> chunk) {
		if (chunk.empty()) {
			return;
		}
		saw_compressed_output = true;
		appendEncryptedFrames(
			profile_vec, chunk, stream_state, cipher_chunk, max_profile_size);
	};
	produce_payload(consume_payload);

	if (!saw_compressed_output) {
		throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
	}

	// Close the secretstream with an empty TAG_FINAL frame (~21 bytes overhead).
	appendEncryptedFrames(
		profile_vec,
		std::span<const Byte>{},
		stream_state,
		cipher_chunk,
		max_profile_size,
		crypto_secretstream_xchacha20poly1305_TAG_FINAL
	);

	writeKdfMetadata(profile_vec, offsets, salt, stream_header, CORRUPT_PROFILE_ERROR);
}

} // namespace

void encryptCompressedFileToProfile(
	SensitiveU64& out_pin,
	vBytes& profile_vec,
	int data_fd,
	std::size_t data_file_size,
	const std::string& data_filename,
	bool is_compressed_file,
	bool has_mastodon_option,
	std::size_t max_profile_size) {

	encryptPayloadToProfile(
		out_pin,
		profile_vec,
		data_filename,
		has_mastodon_option,
		max_profile_size,
		[&](const auto& consume_payload) {
			zlibDeflateFd(
				data_fd,
				data_file_size,
				is_compressed_file,
				consume_payload);
		});
}

void encryptPreparedDataToProfile(
	SensitiveU64& out_pin,
	vBytes& profile_vec,
	std::span<const Byte> prepared_data,
	const std::string& data_filename,
	bool has_mastodon_option,
	std::size_t max_profile_size) {

	encryptPayloadToProfile(
		out_pin,
		profile_vec,
		data_filename,
		has_mastodon_option,
		max_profile_size,
		[prepared_data](const auto& consume_payload) {
			consume_payload(prepared_data);
		});
}

std::optional<std::span<const Byte>> findPdvrdtIccpPayload(std::span<const Byte> iccp_data) {
	constexpr std::size_t PROFILE_PREFIX_SIZE = MASTODON_OFFSETS.pdv_signature + PDVRDT_SIG.size();

	if (!bytesEqualAt(iccp_data, 0, PDVRDT_ICCP_PREFIX)) {
		return std::nullopt;
	}
	const std::span<const Byte> compressed = iccp_data.subspan(PDVRDT_ICCP_PREFIX.size());
	if (compressed.empty()) {
		return std::nullopt;
	}

	vBytes profile_prefix;
	try {
		profile_prefix = zlibInflatePrefix(compressed, PROFILE_PREFIX_SIZE);
	} catch (const std::runtime_error&) {
		return std::nullopt;
	}
	if (profile_prefix.size() != PROFILE_PREFIX_SIZE ||
		!hasPdvrdtProfileMarkers(profile_prefix, MASTODON_OFFSETS)) {
		return std::nullopt;
	}
	return compressed;
}

std::uint64_t deriveCarrierKeyFromPin(const SensitiveU64& pin) {
	// Domain separation, so this can never collide with any other use of the PIN.
	static constexpr unsigned char DOMAIN[crypto_generichash_KEYBYTES] = {
		'p','d','v','r','d','t',' ','c','a','r','r','i','e','r',' ','v',
		'5',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	};

	std::array<Byte, sizeof(std::uint64_t)> pin_bytes{};
	ScopedWipe pin_bytes_wipe{pin_bytes};
	for (std::size_t i = 0; i < pin_bytes.size(); ++i) {
		pin_bytes[i] = static_cast<Byte>(pin.value >> (8 * (pin_bytes.size() - 1 - i)));
	}

	// A full-length digest, with the first 8 bytes taken as the key. Not an
	// 8-byte digest: BLAKE2b binds the output length into its parameter block,
	// so hashing to 8 bytes and truncating a 32-byte hash give different values,
	// and libsodium accepts an out-of-range 8 without complaint while stricter
	// wrappers (alkali, in the Rust port) reject it. Fixing the length here keeps
	// every implementation deriving the same key.
	std::array<Byte, crypto_generichash_BYTES> digest{};
	ScopedWipe digest_wipe{digest};
	if (crypto_generichash(
			digest.data(), digest.size(),
			pin_bytes.data(), pin_bytes.size(),
			DOMAIN, sizeof(DOMAIN)) != 0) {
		throw std::runtime_error("KDF Error: Unable to derive carrier key.");
	}

	std::uint64_t key = 0;
	for (std::size_t i = 0; i < sizeof(key); ++i) {
		key = (key << 8) | digest[i];
	}
	return key;
}

std::optional<std::string> decryptDataFileWithPin(
	vBytes& png_vec,
	const SensitiveU64& recovery_pin,
	bool is_mastodon_file) {
	const auto& offsets = is_mastodon_file ? MASTODON_OFFSETS : DEFAULT_OFFSETS;

	constexpr const char* CORRUPT_FILE_ERROR = "File Recovery Error: Embedded profile is corrupt.";
	requireSpanRange(png_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_FILE_ERROR);
	if (offsets.encrypted_file > png_vec.size()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	if (!hasSupportedKdfMetadataAt(png_vec, offsets.kdf_metadata)) {
		throw std::runtime_error(
			"File Decryption Error: Unsupported legacy encrypted file format. "
			"Use an older pdvrdt release to recover this file.");
	}

	Key key{};
	ScopedWipe key_wipe{key};
	const KdfSecrets secrets = readKdfSecrets(png_vec, offsets, CORRUPT_FILE_ERROR);
	deriveKeyFromPin(key, recovery_pin, secrets.salt);

	const std::size_t ciphertext_length = png_vec.size() - offsets.encrypted_file;
	if (ciphertext_length < minimumStreamCipherSize()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	if (!decryptWithSecretStreamInPlace(png_vec, offsets.encrypted_file, ciphertext_length, key, secrets.stream_header)) {
		return std::nullopt;
	}

	return extractFilenamePrefix(png_vec);
}

std::optional<std::string> decryptDataFile(vBytes& png_vec, bool is_mastodon_file) {
	SensitiveU64 recovery_pin;
	getPin(recovery_pin);
	return decryptDataFileWithPin(png_vec, recovery_pin, is_mastodon_file);
}
