#include "util.h"
#include "log.h"
using namespace std;

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

int recvline(sock& s, std::string& out)
{
	std::string ans;
	char buff[16];
	while (true)
	{
		buff[0] = buff[1] = 0;
		int ret = s.recv(buff, 1);
		if (ret<0) return ret;
		else if (ret == 0)
			return -2; /// If connection is closed... (BUG)
		ans.push_back(buff[0]);
		if (ans.find("\r\n") != std::string::npos)
		{
			break;
		}
	}
	out = ans;

	return ans.size();
}

bool endwith(const string& str, const string& target)
{
	size_t ans = str.rfind(target);
	return ans == str.size() - target.size();
}

static inline bool urlCharNeedEncode(char c)
{
	if ((c >= 'A'&&c <= 'Z') || (c >= 'a'&&c <= 'z') || (c >= '0'&&c <= '9') ||
		(c == '-') || (c == '_') || (c == '.') || (c == '!') ||
		(c == '~') || (c == '*') || (c == '\'') || (c == '(') ||
		(c == ',') || (c == ')'))
	{
		return false;
	}
	else
	{
		return true;
	}
}

static inline int getHexValue(char c)
{
	if (c >= '0'&&c <= '9')
	{
		return c - '0';
	}
	else if (c >= 'a'&&c <= 'f')
	{
		return c - 'a' + 10;
	}
	else if (c >= 'A'&&c <= 'F')
	{
		return c - 'A' + 10;
	}
	else
	{
		logw("[getHexValue]: Failed to get hex value from char: %c\n", c);
		return -1;
	}
}

int urlencode(const string& url_before, string& out_url_encoded)
{
	string ans;
	for (const auto& c : url_before)
	{
		if (urlCharNeedEncode(c))
		{
			char buff[8] = { 0 };
			sprintf(buff, "%%%02X", (unsigned char)c);
			ans.append(buff);
		}
		else
		{
			ans.push_back(c);
		}
	}
	out_url_encoded = ans;
	return 0;
}

int urldecode_real(const string& url_before, string& out_url_decoded)
{
	string ans;
	int len = url_before.size();
	for (int i = 0; i < len; i++)
	{
		if (url_before[i] == '%')
		{
			int a = getHexValue(url_before[i + 1]);
			int b = getHexValue(url_before[i + 2]);
			char c = a * 16 + b;
			ans.push_back(c);
			i += 2;
		}
		else
		{
			ans.push_back(url_before[i]);
		}
	}
	out_url_decoded = ans;

	return 0;
}

int urldecode(const string& url_before, string& out_url_decoded, map<string, string>& out_param)
{
	size_t idx = url_before.find("?");
	if (idx == string::npos)
	{
		// No parameters. Fallback to urldecode.
		return urldecode_real(url_before, out_url_decoded);
	}
	else // ....?para=value&para2=value
	{
		// okay we have params, yeah?
		out_param.clear();

		string frontpart = url_before.substr(0, idx);
		urldecode_real(frontpart, out_url_decoded);
		int len = url_before.size();
		int now = idx + 1;
		// Things get weird...
		while (true)
		{
			size_t nidx = url_before.find("&", now);
			size_t endpoint;
			if (nidx != string::npos)
			{
				// Still have next part
				endpoint = nidx;
			}
			else
			{
				// Oh we are at the end
				endpoint = len;
			}

			// separate it
			string current = url_before.substr(now, endpoint - now);
			size_t midx = current.find("=");
			if (midx != string::npos)
			{
				// param=
				if (midx == current.size() - 1)
				{
					string a = current.substr(0, midx);
					string ta;
					urldecode_real(a, ta);
					out_param.insert(make_pair(ta, ""));
				}
				// param=value
				else
				{
					string a = current.substr(0, midx);
					string ta;
					urldecode_real(a, ta);
					string b = current.substr(midx + 1);
					string tb;
					urldecode_real(b, tb);
					out_param.insert(make_pair(ta, tb));
				}
			}

			// This is the end.
			if (nidx == string::npos) break;
			now = endpoint + 1;
		}

		return 0;
	}
}

int mymin(int a, int b)
{
	return a < b ? a : b;
}