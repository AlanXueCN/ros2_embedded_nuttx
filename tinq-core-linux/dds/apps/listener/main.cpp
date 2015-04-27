extern "C" {
	int c_main (void (*callback)(const char* msg));
}

#include <iostream>
#include <string>

void callback(const char* msg)
{
	std::cout << "Incoming:" << std::string(msg) << std::endl;
}

int main() 
{
	c_main(callback);
	return 0;
}