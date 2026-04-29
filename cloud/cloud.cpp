#include <cmath>
#include <iostream>
#include <sqlite3.h>
#include <string>
#include "httplib.h"

std::string get_local_ip(){
	struct ifaddrs *myaddrs, *ifa;
	void *in_addr;
	char buf[64];
	std::string ip;
	if(getifaddrs(&myaddrs) != 0){
		perror("getifaddrs");
		exit(1);
	}

	for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next){
		if (ifa->ifa_addr == NULL)
			continue;
		if (!(ifa->ifa_flags & IFF_UP))
			continue;

		switch (ifa->ifa_addr->sa_family){
			case AF_INET:{
				struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
				in_addr = &s4->sin_addr;
				break;
			}

			case AF_INET6:{
				struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
				in_addr = &s6->sin6_addr;
				break;
			}

			default:
				continue;
		}

		if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf))){
			printf("%s: inet_ntop failed!\n", ifa->ifa_name);
		}
		else{
			if(((std::string)ifa->ifa_name) == "wlan0"){
				return buf;
			}
			printf("%s: %s\n", ifa->ifa_name, buf);
		}
	}

	freeifaddrs(myaddrs);
	return ip;
}

int main(int argc, char *argv[]){
	httplib::Server svr;
	char szBuffer[1024];
	svr.set_error_logger([](const httplib::Error& err, const httplib::Request* req) {
		std::cout << err;
		std::cout << req;
	});
	svr.Get("/", [&](const httplib::Request &req, httplib::Response &res) {
		res.set_content("server", "text/html");
		std::cout << "served\n";
	});
	/*svr.Post("/sensor/", [&](const httplib::Request &req, httplib::Response &res) {
		res.set_content("server", "text/html");
	});*/
	std::cout << get_local_ip();
	if(svr.listen("10.200.132.232", 8080)){
		std::cout << true;
	}else {
		std::cout << false;
	}
}

