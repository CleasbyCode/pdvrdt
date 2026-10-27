#include "encryption.h"
#include "compression.h"
#include "io_utils.h"
#include "pin_codec.h"
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
#include <new>
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
	const char* word{"PIN"};
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

	struct RestoreErrors {
		int terminal{};
		int signal_mask{};
	};

	// Restore the terminal before unblocking signals: a pending terminating
	// signal must never be able to strand the terminal in noncanonical/no-echo
	// mode. Each step that fails stays pending, so a later call retries it.
	[[nodiscard]] RestoreErrors restore() noexcept {
		RestoreErrors errors;
		if (terminal_active) {
			if (setTerminalAttributes(old) == 0) {
				terminal_active = false;
			} else {
				errors.terminal = errno;
			}
		}
		closeSignalFd();
		if (signals_blocked) {
			if (sigprocmask(SIG_SETMASK, &old_signal_mask, nullptr) == 0) {
				signals_blocked = false;
			} else {
				errors.signal_mask = errno;
			}
		}
		return errors;
	}

	void restoreNoThrow() noexcept {
		static_cast<void>(restore());
	}

	TermiosGuard(bool is_tty, const char* prompt_word) : word(prompt_word) {
		if (!is_tty) {
			return;
		}

		if (sigemptyset(&guarded_signals) != 0 ||
			sigaddset(&guarded_signals, SIGINT) != 0 ||
			sigaddset(&guarded_signals, SIGTERM) != 0 ||
			sigaddset(&guarded_signals, SIGHUP) != 0 ||
			sigaddset(&guarded_signals, SIGQUIT) != 0 ||
			// Prompt and masking writes can raise SIGPIPE when stdout is a closed
			// pipe. Keep it blocked until terminal restoration, including on errors.
			sigaddset(&guarded_signals, SIGPIPE) != 0 ||
			sigaddset(&guarded_signals, SIGTSTP) != 0) {
			throw std::runtime_error(std::format("Terminal Error: Unable to prepare signal protection for {} entry.", word));
		}
		if (sigprocmask(SIG_BLOCK, &guarded_signals, &old_signal_mask) != 0) {
			const std::error_code ec(errno, std::generic_category());
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to protect {} entry from signals: {}", word, ec.message()));
		}
		signals_blocked = true;

		signal_fd = signalfd(-1, &guarded_signals, SFD_CLOEXEC | SFD_NONBLOCK);
		if (signal_fd < 0) {
			const std::error_code ec(errno, std::generic_category());
			restoreNoThrow();
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to monitor signals during {} entry: {}", word, ec.message()));
		}

		if (tcgetattr(STDIN_FILENO, &old) != 0) {
			const std::error_code ec(errno, std::generic_category());
			restoreNoThrow();
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to read terminal settings for secure {} entry: {}", word, ec.message()));
		}
		termios newt = old;
		const auto mask = static_cast<tcflag_t>(ICANON | ECHO);
		newt.c_lflag &= static_cast<tcflag_t>(~mask);
		if (setTerminalAttributes(newt) != 0) {
			const std::error_code ec(errno, std::generic_category());
			restoreNoThrow();
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to disable terminal echo for secure {} entry: {}", word, ec.message()));
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
		const RestoreErrors errors = restore();
		if (errors.terminal != 0) {
			const std::error_code ec(errors.terminal, std::generic_category());
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to restore terminal settings after {} entry: {}", word, ec.message()));
		}
		if (errors.signal_mask != 0) {
			const std::error_code ec(errors.signal_mask, std::generic_category());
			throw std::runtime_error(std::format(
				"Terminal Error: Unable to restore the signal mask after {} entry: {}", word, ec.message()));
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
				"{} Entry Error: Unable to re-deliver interrupt signal: {}", word, ec.message()));
		}
		// Suspending (Ctrl-Z) deliberately cancels PIN entry rather than resuming
		// it; say so, since the user did not ask for the operation to stop.
		if (signal_number == SIGTSTP) {
			throw std::runtime_error(std::format(
				"{0} Entry Error: {0} entry was cancelled by suspend (Ctrl-Z). Run the command again to enter your {0}.",
				word));
		}
		throw std::runtime_error(std::format("{0} Entry Error: {0} entry interrupted by signal.", word));
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

		const std::uint32_t frame_len = getValue(storage, cipher_pos);
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

// Takes the caller's RecoveryPin by reference: a by-value parameter would make
// a second copy of the secret, and scrubbing a parameter only reaches its stack
// slot, never a register the compiler chose to keep it in. Only the decimal
// rendering of an old PIN is transient here, and that is scrubbed on every path.
//
// KDF3 (a 128-bit KEY) hashes the 16 raw KEY bytes; KDF2 (an old PIN) hashes its
// decimal digits. The caller has already matched the secret to the image.
void deriveKeyFromPin(Key& out_key, const RecoveryPin& pin, const Salt& salt) {
	std::array<char, 32> pin_buf{};
	ScopedWipe pin_buf_wipe{pin_buf};

	const char* password = nullptr;
	unsigned long long password_len = 0;
	if (pin.isKey()) {
		password = reinterpret_cast<const char*>(pin.key.data());
		password_len = pin.key.size();
	} else {
		auto [ptr, ec] = std::to_chars(pin_buf.data(), pin_buf.data() + pin_buf.size(), pin.legacy_value);
		if (ec != std::errc{}) {
			throw std::runtime_error("KDF Error: Failed to encode recovery PIN.");
		}
		password = pin_buf.data();
		password_len = static_cast<unsigned long long>(ptr - pin_buf.data());
	}

	// Wire-format parameters: they are not recorded in the KDF metadata (only the
	// algorithm byte is), so every image ever made depends on these exact values.
	// Pinned here rather than taken from libsodium's INTERACTIVE presets, which
	// are free to change between releases. 2 / 64 MiB is what those presets have
	// always been for Argon2id13, so existing images are unaffected.
	constexpr unsigned long long PAYLOAD_KDF_OPSLIMIT = 2;
	constexpr std::size_t PAYLOAD_KDF_MEMLIMIT = 64uz * 1024 * 1024;

	const int rc = crypto_pwhash(
		out_key.data(),
		out_key.size(),
		password,
		password_len,
		salt.data(),
		PAYLOAD_KDF_OPSLIMIT,
		PAYLOAD_KDF_MEMLIMIT,
		crypto_pwhash_ALG_ARGON2ID13
	);

	if (rc != 0) {
		throw std::runtime_error("KDF Error: Unable to derive encryption key.");
	}
}

// Fills `out_pin` in place with a fresh 128-bit KEY (never all zero); see
// RecoveryPin for why no secret is returned by value anywhere in this file.
void generateRecoveryKey(RecoveryPin& out_pin) {
	out_pin.clear();
	Byte any = 0;
	while (any == 0) {
		randombytes_buf(out_pin.key.data(), out_pin.key.size());
		any = 0;
		for (const Byte b : out_pin.key) any |= b;
	}
	out_pin.kind = RecoveryPin::Kind::key128;
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
	std::memcpy(region + KDF_MAGIC_OFFSET, KDF_METADATA_MAGIC_V3.data(), KDF_METADATA_MAGIC_V3.size());
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

namespace {

// Deliberately says nothing about which rule the input broke: the shape of a
// recovery KEY/PIN is public, but how far a given attempt got is not worth
// echoing. Each prompt describes only the forms its image can take.
[[nodiscard]] const char* pinFormatError(PinPrompt prompt) noexcept {
	switch (prompt) {
		case PinPrompt::key:
			return "KEY Entry Error: That is not a well-formed recovery KEY.\n\n"
				"A pdvrdt KEY is 24 letters and digits, printed in groups of four "
				"(XXXX-XXXX-XXXX-XXXX-XXXX-XXXX).\n\n"
				"Enter it exactly as it was printed when the file was concealed.";
		case PinPrompt::pin:
			return "PIN Entry Error: That is not a well-formed recovery PIN.\n\n"
				"This image was made before pdvrdt 6.0 and uses a PIN of up to 20 digits.\n\n"
				"Enter it exactly as it was printed when the file was concealed.";
		case PinPrompt::key_or_pin:
			break;
	}
	return "KEY/PIN Entry Error: That is not a well-formed recovery KEY/PIN.\n\n"
		"A pdvrdt KEY is 24 letters and digits, printed in groups of four "
		"(XXXX-XXXX-XXXX-XXXX-XXXX-XXXX);\n"
		"Images made before pdvrdt 6.0 use a PIN of up to 20 digits.\n\n"
		"Enter it exactly as it was printed when the file was concealed.";
}

// A 24-character KEY whose check pair does not match its body: a typo the user
// can find, reported before any key derivation. An image made before 6.0 takes
// no KEY at all, so there the entry is simply not a well-formed PIN.
[[nodiscard]] const char* pinCheckError(PinPrompt prompt) noexcept {
	switch (prompt) {
		case PinPrompt::key:
			return "KEY Entry Error: That KEY has a typo: its last two characters do not "
				"match the rest. Check each character, including upper and lower case.";
		case PinPrompt::pin:
			return pinFormatError(prompt);
		case PinPrompt::key_or_pin:
			break;
	}
	return "KEY/PIN Entry Error: That KEY has a typo: its last two characters do not "
		"match the rest. Check each character, including upper and lower case.";
}

} // namespace

void getPin(PinPrompt prompt, RecoveryPin& out_pin) {
	// 24 characters plus 5 group dashes, with room to spare.
	constexpr std::size_t MAX_PIN_KEYSTROKES = 32;
	const char* const word = pinPromptWord(prompt);
	out_pin.clear();

	const bool is_tty = (isatty(STDIN_FILENO) != 0);
	TermiosGuard termios_guard{is_tty, word};

	const auto displayError = [word] {
		return std::runtime_error(std::format("{} Entry Error: Failed to display masked {} input.", word, word));
	};

	std::print("\n{}: ", word);
	if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
		throw std::runtime_error(std::format(
			"{} Entry Error: Failed to display the recovery {} prompt.", word, word));
	}

	// A fixed stack buffer, never a heap string, wiped on every exit path.
	std::array<char, MAX_PIN_KEYSTROKES> input{};
	ScopedWipe input_wiper{input};
	std::size_t input_len = 0;
	char ch{};
	ScopedWipe ch_wiper{ch};
	// Keystrokes typed past the limit are not stored, but they are counted, so
	// a backspace undoes the keystroke the user actually made last. Without
	// this, backspacing back under the limit would hand out a secret built
	// from the first characters entered.
	std::size_t dropped_keys = 0;
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
		// Spaces and tabs are ignored, so a KEY pasted with stray whitespace
		// still works.
		if (ch == ' ' || ch == '\t') continue;
		if (ch == '\b' || ch == 127) {
			if (dropped_keys > 0) {
				// Undo an over-limit keystroke; nothing was echoed for it.
				--dropped_keys;
			} else if (input_len > 0) {
				if (termios_guard.masksInput()) {
					std::print("\b \b");
					if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
						wipe_input();
						throw displayError();
					}
				}
				input[--input_len] = '\0';
			}
			continue;
		}
		// Every other keystroke is kept -- including the group dash, so that a
		// backspace removes the dash the user typed -- and so that a stray
		// character rejects the entry below instead of being silently dropped.
		if (input_len >= input.size()) {
			++dropped_keys;
			continue;
		}
		input[input_len++] = ch;
		if (termios_guard.masksInput()) {
			// Dashes are layout, not secret; echoing them keeps the user's
			// place in the groups.
			std::print("{}", ch == '-' ? '-' : '*');
			if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
				wipe_input();
				throw displayError();
			}
		}
	}

	std::println("");
	if (std::fflush(stdout) != 0 || std::ferror(stdout) != 0) {
		wipe_input();
		throw displayError();
	}
	termios_guard.finish();

	if (read_error) {
		wipe_input();
		throw std::runtime_error(std::format("{} Entry Error: Failed to read recovery {}.", word, word));
	}

	// Knowable mistakes are reported now, before an Argon2id run that could
	// only end in the ambiguous "invalid KEY/PIN or file is corrupt". The codec
	// owns the grammar (old decimal PIN, or 24-character KEY with a check pair).
	if (dropped_keys > 0) {
		wipe_input();
		throw std::runtime_error(pinFormatError(prompt));
	}
	const auto parsed = parseRecoveryPinText(std::string_view(input.data(), input_len), out_pin);
	wipe_input();
	if (!parsed) {
		throw std::runtime_error(
			parsed.error() == PinParseError::checksum ? pinCheckError(prompt) : pinFormatError(prompt));
	}
}

namespace {

constexpr const char* ENCRYPTED_OVERFLOW_ERROR = "File Size Error: Encrypted output overflow.";
constexpr std::size_t STREAM_FRAME_OVERHEAD =
	STREAM_FRAME_LEN_BYTES + crypto_secretstream_xchacha20poly1305_ABYTES;

// Bytes appendEncryptedFrames() adds for `plaintext_size` bytes of non-final
// plaintext: the data plus a length field and auth tag per frame.
[[nodiscard]] std::size_t framedCipherSize(std::size_t plaintext_size) {
	if (plaintext_size == 0) return 0;
	const std::size_t frame_count = 1 + ((plaintext_size - 1) / STREAM_CHUNK_SIZE);
	return checkedAddSize(
		plaintext_size,
		checkedMulSize(frame_count, STREAM_FRAME_OVERHEAD, ENCRYPTED_OVERFLOW_ERROR),
		ENCRYPTED_OVERFLOW_ERROR);
}

// Exact size of the profile encryptPayloadToProfile() builds from a template of
// `template_size` bytes, a `payload_size`-byte payload handed over in one piece
// (or in pieces that are whole multiples of the frame size), and a filename of
// `filename_length` bytes: stream header, filename frame, payload frames and
// the empty final frame.
[[nodiscard]] std::size_t encryptedProfileSize(
	std::size_t template_size,
	std::size_t payload_size,
	std::size_t filename_length) {

	std::size_t profile_size = checkedAddSize(
		template_size, crypto_secretstream_xchacha20poly1305_HEADERBYTES, ENCRYPTED_OVERFLOW_ERROR);
	profile_size = checkedAddSize(profile_size, framedCipherSize(1 + filename_length), ENCRYPTED_OVERFLOW_ERROR);
	profile_size = checkedAddSize(profile_size, framedCipherSize(payload_size), ENCRYPTED_OVERFLOW_ERROR);
	return checkedAddSize(profile_size, STREAM_FRAME_OVERHEAD, ENCRYPTED_OVERFLOW_ERROR);
}

// How a profile's buffer is sized while frames are appended to it.
//
// When the payload size is known -- stored input, or prepared data -- the final
// size is reserved up front, capped at the output limit. Growing frame by frame
// instead would, near the 2 GiB default limit, briefly hold both the old and the
// doubled buffer -- about 3 GiB -- on a reallocation.
//
// For deflated input the estimate is only an upper bound, and a compressible
// payload can come in far below it: reserving the bound would ask a 1 GiB file
// of zeros for ~1 GiB of address space to hold a profile of a few MiB. So that
// case starts at a bounded size. If the profile outgrows it, the bound is tried
// next: it is only address space until written, and an incompressible payload
// then costs a single copy of the initial reservation, where doubling would end
// with the old buffer and a near-final one both resident -- about twice the
// profile. Where that speculative reservation is refused (an address-space
// limit, say), growth falls back to doubling, never past the bound while the
// profile still fits under it.
class ProfileReservation {
public:
	static constexpr std::size_t INITIAL_DEFLATED_RESERVE = 64uz * 1024 * 1024;

	ProfileReservation(
		vBytes& profile_vec,
		std::size_t estimated_size,
		bool estimate_is_exact,
		std::size_t max_profile_size)
		: profile_vec_(profile_vec),
		  max_size_(max_profile_size),
		  ceiling_(std::min(estimated_size, max_profile_size)) {
		reserve(estimate_is_exact ? ceiling_ : std::min(ceiling_, INITIAL_DEFLATED_RESERVE));
	}

	// Make room for `extra` more bytes. A size past the output limit is left to
	// appendEncryptedFrame(), which reports it.
	void ensureRoomFor(std::size_t extra) {
		const std::size_t capacity = profile_vec_.capacity();
		if (extra > max_size_ || profile_vec_.size() > max_size_ - extra) return;
		const std::size_t needed = profile_vec_.size() + extra;
		if (needed <= capacity) return;

		if (needed <= ceiling_ && !speculation_refused_) {
			try {
				reserve(ceiling_);
				return;
			} catch (const std::bad_alloc&) {
				speculation_refused_ = true;
			}
		}
		const std::size_t doubled = capacity > max_size_ / 2 ? max_size_ : capacity * 2;
		std::size_t target = std::max(doubled, needed);
		if (needed <= ceiling_) target = std::min(target, ceiling_);
		reserve(target);
	}

private:
	void reserve(std::size_t target) {
		if (target > profile_vec_.capacity()) profile_vec_.reserve(target);
	}

	vBytes& profile_vec_;
	std::size_t max_size_;
	std::size_t ceiling_;
	bool speculation_refused_{false};
};

template <typename PayloadProducer>
void encryptPayloadToProfile(
	RecoveryPin& out_pin,
	vBytes& profile_vec,
	const std::string& data_filename,
	bool has_mastodon_option,
	std::size_t max_profile_size,
	std::size_t estimated_payload_size,
	bool estimate_is_exact,
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
	ProfileReservation reservation(
		profile_vec,
		encryptedProfileSize(profile_vec.size(), estimated_payload_size, data_filename.size()),
		estimate_is_exact,
		max_profile_size);

	Key key{};
	ScopedWipe key_wipe{key};
	Salt salt{};
	StreamHeader stream_header{};
	// Generated straight into the caller's storage: there is never a second copy
	// to scrub, and the caller's RecoveryPin wipes it on every path out of here,
	// exception or not.
	generateRecoveryKey(out_pin);

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
		reservation.ensureRoomFor(framedCipherSize(chunk.size()));
		appendEncryptedFrames(
			profile_vec, chunk, stream_state, cipher_chunk, max_profile_size);
	};
	produce_payload(consume_payload);

	if (!saw_compressed_output) {
		throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
	}

	// Close the secretstream with an empty TAG_FINAL frame (~21 bytes overhead).
	reservation.ensureRoomFor(STREAM_FRAME_OVERHEAD);
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
	RecoveryPin& out_pin,
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
		// Exact for stored input; an upper bound for deflated input.
		zlibPayloadStreamBound(data_file_size, is_compressed_file),
		is_compressed_file,
		[&](const auto& consume_payload) {
			zlibDeflateFd(
				data_fd,
				data_file_size,
				is_compressed_file,
				consume_payload);
		});
}

void encryptPreparedDataToProfile(
	RecoveryPin& out_pin,
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
		prepared_data.size(),
		true,
		[prepared_data](const auto& consume_payload) {
			consume_payload(prepared_data);
		});
}

std::size_t computeRedditEncryptedProfileSize(
	std::size_t prepared_data_size,
	std::size_t filename_length) {

	if (filename_length == 0 ||
		filename_length > static_cast<std::size_t>(std::numeric_limits<Byte>::max())) {
		throw std::runtime_error("Data File Error: Invalid data filename length.");
	}
	return encryptedProfileSize(DEFAULT_OFFSETS.encrypted_file, prepared_data_size, filename_length);
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

namespace {

// The carrier KDFs hash an old PIN as eight big-endian bytes.
using PinBytes = std::array<Byte, sizeof(std::uint64_t)>;

void pinToBigEndian(const RecoveryPin& pin, PinBytes& out) noexcept {
	for (std::size_t i = 0; i < out.size(); ++i) {
		out[i] = static_cast<Byte>(pin.legacy_value >> (8 * (out.size() - 1 - i)));
	}
}

constexpr unsigned long long CARRIER_KDF_OPSLIMIT = 2;
constexpr std::size_t CARRIER_KDF_MEMLIMIT = 64uz * 1024 * 1024;

// Argon2id with the carrier parameters, then keyed BLAKE2b-256 of `domain`
// under the whole Argon2 output.
void deriveChachaCarrierKey(
	std::span<const Byte> password,
	const Salt& salt,
	std::string_view domain,
	std::array<Byte, 32>& digest,
	std::array<Byte, CarrierKeys::CHACHA_KEY_BYTES>& out_key) {
	if (crypto_pwhash(
			digest.data(), digest.size(),
			reinterpret_cast<const char*>(password.data()), password.size(),
			salt.data(), CARRIER_KDF_OPSLIMIT, CARRIER_KDF_MEMLIMIT,
			crypto_pwhash_ALG_ARGON2ID13) != 0) {
		throw std::runtime_error("KDF Error: Unable to derive Reddit carrier key.");
	}
	if (crypto_generichash(
			out_key.data(), out_key.size(),
			reinterpret_cast<const unsigned char*>(domain.data()), domain.size(),
			digest.data(), digest.size()) != 0) {
		throw std::runtime_error("KDF Error: Unable to derive Reddit carrier key.");
	}
}

// The first eight digest bytes, read big-endian, are the carrier key.
void carrierKeyFromDigest(std::span<const Byte> digest, SensitiveU64& out_key) noexcept {
	out_key.value = std::ranges::fold_left(
		digest.first(sizeof(std::uint64_t)),
		std::uint64_t{0},
		[](std::uint64_t key, Byte byte) static { return (key << 8) | byte; });
}

} // namespace

void deriveCarrierKeysFromPin(const RecoveryPin& pin, CarrierKeys& out_keys) {
	// Wire-format parameters, deliberately fixed rather than libsodium's
	// changeable INTERACTIVE presets. The salt cannot be read from the encrypted
	// payload until the carrier has been located, so use a separate, fixed
	// domain salt. That permits cross-image amortization, but every candidate
	// still requires an Argon2id evaluation, and a 128-bit KEY cannot be
	// searched at all.
	static constexpr Salt KEY_DOMAIN_SALT = {
		'p','d','v','r','d','t','c','a','r','r','i','e','r','-','v','4'
	};
	// The "-v2" salt is kept for version 3 so one derivation yields both keys.
	static constexpr Salt PIN_DOMAIN_SALT = {
		'p','d','v','r','d','t','c','a','r','r','i','e','r','-','v','2'
	};
	constexpr Byte KEY_CARRIER_VERSION = 4;
	constexpr Byte PIN_CARRIER_VERSION = 3;

	std::array<Byte, 32> digest{};
	ScopedWipe digest_wipe{digest};

	if (pin.isKey()) {
		deriveChachaCarrierKey(pin.key, KEY_DOMAIN_SALT, "pdvrdt carrier v4", digest, out_keys.chacha);
		out_keys.chacha_version = KEY_CARRIER_VERSION;
		out_keys.has_v2 = false;
		out_keys.v2.value = 0;
		return;
	}
	if (pin.kind != RecoveryPin::Kind::legacy_pin) {
		throw std::runtime_error("KDF Error: Missing recovery KEY.");
	}

	PinBytes pin_bytes{};
	ScopedWipe pin_bytes_wipe{pin_bytes};
	pinToBigEndian(pin, pin_bytes);
	deriveChachaCarrierKey(pin_bytes, PIN_DOMAIN_SALT, "pdvrdt carrier v3", digest, out_keys.chacha);
	out_keys.chacha_version = PIN_CARRIER_VERSION;
	// The whole digest keyed the hash above, so the version-3 key is unrelated
	// to the 64-bit version-2 key taken from its first eight bytes.
	carrierKeyFromDigest(digest, out_keys.v2);
	out_keys.has_v2 = true;
}

void deriveLegacyCarrierKeyFromPin(const RecoveryPin& pin, SensitiveU64& out_key) {
	if (pin.kind != RecoveryPin::Kind::legacy_pin) {
		throw std::runtime_error("KDF Error: Version-1 carriers take a numeric PIN.");
	}
	// Recovery only: version-1 carriers used this inexpensive hash and remain
	// vulnerable to cheap PIN checks. Never use it when creating a new carrier.
	static constexpr unsigned char DOMAIN[crypto_generichash_KEYBYTES] = {
		'p','d','v','r','d','t',' ','c','a','r','r','i','e','r',' ','v',
		'5',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	};

	PinBytes pin_bytes{};
	ScopedWipe pin_bytes_wipe{pin_bytes};
	pinToBigEndian(pin, pin_bytes);

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
	carrierKeyFromDigest(digest, out_key);
}

std::optional<std::string> decryptDataFileWithPin(
	vBytes& png_vec,
	const RecoveryPin& recovery_pin,
	bool is_mastodon_file) {
	const auto& offsets = is_mastodon_file ? MASTODON_OFFSETS : DEFAULT_OFFSETS;

	constexpr const char* CORRUPT_FILE_ERROR = "File Recovery Error: Embedded profile is corrupt.";
	requireSpanRange(png_vec, offsets.kdf_metadata, KDF_METADATA_REGION_BYTES, CORRUPT_FILE_ERROR);
	if (offsets.encrypted_file > png_vec.size()) {
		throw std::runtime_error(CORRUPT_FILE_ERROR);
	}

	const auto kdf_version = kdfMetadataVersionAt(png_vec, offsets.kdf_metadata);
	if (!kdf_version) {
		throw std::runtime_error(
			"File Decryption Error: Unsupported legacy encrypted file format. "
			"Use an older pdvrdt release to recover this file.");
	}
	// A KEY for an old PIN image, or a PIN for a KEY image, is simply the wrong
	// secret. Say so without spending an Argon2id pass on it.
	if (recovery_pin.isKey() != (*kdf_version == KdfVersion::key_v3)) {
		return std::nullopt;
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
