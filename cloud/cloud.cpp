#include <sqlite3.h>
#include "httplib.h"

int main(int argc, char *argv[]){
	httplib::Server svr;
	svr.Get("/", [&](const httplib::Request &req, httplib::Response &res) {
		res.set_content("server", "text/html");
	});
	svr.listen("192.168.0.19", 8080);	
}

