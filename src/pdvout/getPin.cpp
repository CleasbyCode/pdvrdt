uint64_t getPin() {
    constexpr uint64_t MAX_UINT64 = std::numeric_limits<uint64_t>::max();
    std::string input;
    char ch; 
    bool sync_status = std::cout.sync_with_stdio(false);

#ifdef _WIN32
    while (input.length() < 20) { 
	    ch = _getch();
	    if (ch >= '0' && ch <= '9') {
		    input.push_back(ch);
		    std::cout << '*' << std::flush;
	    } else if (ch == '\b' && !input.empty()) {
		    std::cout << "\b \b" << std::flush;
		    input.pop_back();
	    } else if (ch == '\r') {
		    break;
	    }
    }
#else
	struct termios oldt, newt;
    	tcgetattr(STDIN_FILENO, &oldt);
    	newt = oldt;
    	newt.c_lflag &= ~(ICANON | ECHO);
    	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
   while (input.length() < 20) {
	   ssize_t bytesRead = read(STDIN_FILENO, &ch, 1); 
	   if (bytesRead <= 0) continue; 
	   
	   if (ch >= '0' && ch <= '9') {
		   input.push_back(ch);
		   std::cout << '*' << std::flush; 
	   } else if ((ch == '\b' || ch == 127) && !input.empty()) {  
		   std::cout << "\b \b" << std::flush;
		   input.pop_back();
	   } else if (ch == '\n') {
		   break;
	   }
   }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); 

#endif

std::cout << std::endl; 
std::cout.sync_with_stdio(sync_status);

uint64_t user_pin;
if (input.empty() || input.length() > 20 || std::stoull(input) > MAX_UINT64) {
	user_pin = 0; 
} else {
	user_pin = std::stoull(input); 
}
	return user_pin;
}
