#include <cmath>
#include <iostream>
#include <shared_mutex>
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
				printf("%s: %s\n", ifa->ifa_name, buf);
				return buf;
			}
		}
	}

	freeifaddrs(myaddrs);
	return ip;
}

int main(int argc, char *argv[]){
	sqlite3* DB;
	{//boş sql işlemleri için blok
	std::string user="CREATE TABLE IF NOT EXISTS users("
					"yuzde INT NOT NULL ,
					"");";
	
	std::string post="CREATE TABLE IF NOT EXISTS users("
					"postid INT PRIMARY KEY NOT NULL, "
					"FOREIGN KEY(posterid) REFERENCES user(id)"
					"konum TEXT"
					"time INT"get_local_ip
					"message TEXT);";
	int exit = 0;
	exit = sqlite3_open("example.db", &DB);
	char* messaggeError;
	exit = sqlite3_exec(DB, user.c_str(), NULL, 0, &messaggeError);

	if (exit != SQLITE_OK) {
		// TODO handle error
	}
	else{
		std::cout << "Table user created Successfully" << std::endl;
		exit = sqlite3_exec(DB, post.c_str(), NULL, 0, &messaggeError);
		if (exit != SQLITE_OK) {
			// TODO handle error
		}
		else{
			std::cout << "Table listing created Successfully" << std::endl;
		}
	}
	}
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
	std::string ip = get_local_ip();
	std::cout << ip << "\n";
	if(svr.listen(ip, 8080)){
		std::cout << true;
	}else {
		std::cout << false;
	}
}

