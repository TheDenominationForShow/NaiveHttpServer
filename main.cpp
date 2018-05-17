#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <io.h> // access
#include "GSock/gsock.h"
#include "NaiveThreadPool/ThreadPool.h"
using namespace std;

#ifdef HCDEBUG
#define dprintf(fmt,...) printf(fmt,__VA_ARGS__)
#else
#define dprintf(fmt,...)
#endif

#define BIND_PORT 8000
static const string SERVER_ROOT = ".";

int sendn(sock& s, const std::string& in_data)
{
	int done = 0;
	int total = in_data.size();
	while (done < total)
	{
		int ret = s.send(in_data.c_str() + done, total - done);
		if (ret <= 0)
		{
			return ret;
		}
		else
		{
			done += ret;
		}
	}
	return done;
}

int recvline(sock& s,std::string& out)
{
	std::string ans;
	char buff[16];
	while(true)
	{
		buff[0]=buff[1]=0;
		int ret=s.recv(buff,1);
		if(ret<0) return ret;
		else if(ret == 0) 
			return -2; /// If connection is closed... (BUG)
		ans.push_back(buff[0]);
		if(ans.find("\r\n")!=std::string::npos)
		{
			break;
		}
	}
	out=ans;

	return ans.size();
}

int recvheader_raw(sock& s,std::string& header_raw)
{
	std::string ans;
	while(true)
	{
		std::string tmp;
		int ret=recvline(s,tmp);
		if(ret<0) return ret;
		ans.append(tmp);
		if(ans.find("\r\n\r\n")!=std::string::npos)
		{
			break;
		}
	}
	header_raw=ans;
	return ans.size();
}

// Notice: \r\n at end of each line is removed.
int split_header_raw(const string& header_raw,vector<string>& outvec)
{
	int now = 0;
	size_t target;
	vector<string> vec;
	while (string::npos != (target = header_raw.find("\r\n", now)))
	{
		if (target - now == 0)
		{
			// This is the final \r\n\r\n, stop.
			break;
		}
		vec.push_back(header_raw.substr(now, target - now));
		now = target + 2;
	}
	outvec = move(vec); // move would be faster?
	return 0;
}

int parse_header_rawline(const string& header_rawline, pair<string, string>& outpr)
{
	size_t target;
	if (string::npos == (target = header_rawline.find(":")))
	{
		return -1;
	}
	else
	{
		outpr.first = header_rawline.substr(0, target);
		// Skip space
		target++;
		while (header_rawline[target] == ' ') target++;
		outpr.second = header_rawline.substr(target);
		return 0;
	}
}

int parse_header_firstline(const string& http_firstline, string& method, string& path, string& http_version)
{
	size_t target = 0;
	if (string::npos == (target = http_firstline.find(" ")))
	{
		return -1;
	}
	method = http_firstline.substr(0, target);
	target++;
	if (string::npos == (target = http_firstline.find(" ", target)))
	{
		return -2;
	}
	path = http_firstline.substr(method.size() + 1, target - method.size() - 1);
	http_version = http_firstline.substr(target + 1);
	return 0;
}

int parse_header(const string& header_raw,string& method,string& path,string& http_version,map<string,string>& outmap)
{
	vector<string> header_vec;
	int ret = split_header_raw(header_raw, header_vec);
	if (ret < 0)
	{
		return -1;
	}
	
	if (parse_header_firstline(header_vec[0], method, path, http_version) < 0)
	{
		return -2;
	}

	map<string, string> header_map;
	int sz = header_vec.size();
	for (int i = 1; i < sz; i++)
	{
		pair<string, string> pr;
		if (parse_header_rawline(header_vec[i], pr) < 0)
		{
			return -3;
		}
		else
		{
			header_map.insert(pr);
		}
	}
	outmap = move(header_map);
	return 0;
}

int get_request_type(const string& path)
{
	string realpath = SERVER_ROOT + path;
	if (access(realpath.c_str(), 0) < 0) // File not exist
	{
		// File not exist, maybe dynamic request?
		// Only Lua extension is planned to support, which means *.php will be treated as a static file.
		realpath = realpath + ".lua";
		if (access(realpath.c_str(), 0) < 0)
		{
			// Not a valid request.
			return -1;
		}
		else
		{
			/// Dynamic Request
			return 1;
		}
	}
	else // File exists.
	{
		if (path.find(".lua") == path.size() - 4) // XXX.lua
		{
			/// Dynamic Request
			return 1;
		}
		else
		{
			/// Static Request
			/// Notice: Binary file should be dynamic request (like CGI), but here just deal it as static file.
			return 0;
		}
	}
}

int GetFileContent(const string& request_path, string& out_content)
{
	string realpath = SERVER_ROOT + request_path;
	FILE* fp = fopen(realpath.c_str(), "rb");
	if (fp == NULL) return -1;
	char buff[1024];
	string content;
	while (true)
	{
		memset(buff, 0, 1024);
		int ret = fread(buff, 1, 1024, fp);
		if (ret <= 0)
		{
			break;
		}
		content.append(string(buff, ret));
	}
	fclose(fp);
	out_content = content;
	return 0;
}

class Response
{
public:
	/// Set code will reset response status
	void set_code(int code)
	{
		header = "HTTP/1.1 ";
		switch (code)
		{
		case 200:
			header.append("200 OK");
			break;
		case 404:
			header.append("404 Not Found");
			break;
		case 500:
			header.append("500 Internal Server Error");
			break;
		case 501:
			header.append("501 Not Implemented");
			break;
		}
		header.append("\r\n");
	}

	void set_raw(const string& name, const string& value)
	{
		mp[name] = value;
	}

	void setContentLength(int length)
	{
		set_raw("Content-Length", to_string(length));
	}

	void setContentType(const string& content_type)
	{
		set_raw("Content-Type", content_type);
	}

	void setContent(const string& content,const string& content_type="text/html")
	{
		setContentLength(content.size());
		setContentType(content_type);
		data = content;
	}

	string toString()
	{
		string ans;
		ans.append(header);
		for (auto& pr : mp)
		{
			ans.append(pr.first + ": " + pr.second + "\r\n");
		}
		ans.append("\r\n");
		if (!data.empty())
		{
			ans.append(data);
		}
		return ans;
	}

	int send_with(sock& s)
	{
		/// Server does not support keep-alive connection.
		set_raw("Connection", "close");
		string t = toString();
		printf("%s\n", t.c_str());
		return sendn(s, t);
	}
private:
	string header;
	map<string, string> mp;
	string data;
};

void response_end(string& response)
{
	response.append("\r\n");
}

int request_get_handler(sock& s, const string& path, const string& version, const map<string, string>& mp)
{
	int request_type = get_request_type(path);
	if (request_type < 0)
	{
		Response r;
		r.set_code(404);
		r.send_with(s);
		return 0;
	}
	if (request_type == 0)
	{
		// Static Target
		// Just read out and send it.
		string content;
		if (GetFileContent(path, content) < 0)
		{
			/// File not readable.
			Response r;
			r.set_code(500);
			r.send_with(s);
			return 0;
		}
		else
		{
			Response r;
			r.set_code(200);
			r.setContent(content);
			r.send_with(s);
			return 0;
		}
	}
	else
	{
		// Dynamic Target
		// Currently not supported.
		Response r;
		r.set_code(501);
		r.send_with(s);
		return 0;
	}
}

int request_handler(sock& s)
{
	printf("RequestHandler sock(%p): Started\n", &s);
	string header_raw;
	int ret=recvheader_raw(s,header_raw);
	if(ret<0)
	{
		return -1;
	}
	printf("RequestHandler sock(%p): Header Received.\n", &s);
	string method;
	string path;
	string version;
	map<string, string> mp;
	ret = parse_header(header_raw, method, path, version, mp);
	printf("RequestHandler sock(%p): Header Parse Finished.\n", &s);
	if (ret < 0)
	{
		return -2;
	}

	printf("==========sock(%p)=========\nMethod: %s\nPath: %s\nVersion: %s\n", &s, method.c_str(), path.c_str(), version.c_str());
	for (auto& pr : mp)
	{
		printf("%s\t %s\n", pr.first.c_str(), pr.second.c_str());
	}
	printf("^^^^^^^^^^sock(%p)^^^^^^^^^^\n", &s);
	printf("RequestHandler sock(%p): Finished Successfully.\n", &s);

	if (method == "GET")
	{
		if (request_get_handler(s, path, version, mp) < 0)
		{
			return -3;
		}
	}
	
	return 0;
}



int main()
{
	dprintf("NaiveHTTPServer Started.\n");
	serversock t;
	if(t.bind(BIND_PORT)<0)
	{
		dprintf("Failed to bind at port %d\n",BIND_PORT);
		return 0;
	}
	if(t.listen(10)<0)
	{
		dprintf("Failed to listen at port %d\n",BIND_PORT);
	}
	dprintf("Server started at port %d\n",BIND_PORT);
	dprintf("Starting thread pool...\n");
	ThreadPool tp(10);
	dprintf("Server is now ready for connections.\n");
	while(true)
	{
		sock* ps=new sock;
		int ret=t.accept(*ps);
		if(ret<0)
		{
			dprintf("Failed to accept connection. Abort.\n");
			break;
		}
		if(tp.start([ps](){
					int ret=request_handler(*ps);
					dprintf("request handler returns %d\n",ret);
					delete ps;
				})<0)
		{
			dprintf("Failed to start job at thread pool.\n");
		}
		else
		{
			dprintf("Job started with sock: %p\n",ps);
		}
	}
	dprintf("Server closed.\n");

	return 0;
}

