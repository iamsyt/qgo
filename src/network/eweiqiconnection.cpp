#include "eweiqiconnection.h"

EWeiQiConnection::EWeiQiConnection(NetworkDispatch * _dispatch, const class ConnectionInfo & info)
: TygemConnection(_dispatch, info)
{
	serverCodec = QTextCodec::codecForName("GB2312");
	//error handling!
	requestServerInfo();
}

int EWeiQiConnection::requestServerInfo(void)
{
	qDebug("Requesting eWeiQi Server Info");
	ConnectionInfo server("121.189.9.52", 80, "", "", TypeNone);
	if(!openConnection(server))
	{
		qDebug("Can't get server info");
		return -1;
	}
	unsigned int length = 0x96;
	unsigned char * packet = new unsigned char[length];  //term 0x00
	snprintf((char *)packet, length,
		 "GET /service/down_china/livebaduk3.cfg HTTP/1.1\r\n" \
		 "User-Agent: Tygem HTTP\r\n" \
		 "Host: service.tygem.com\r\n" \
		 "Connection: Keep-Alive\r\n" \
		 "Cache-Control: no-cache\r\n\r\n");
	for(int i = 0; i < length; i++)
		printf("%02x", packet[i]);
	printf("\n");
	if(write((const char *)packet, length) < 0)
	{
		qWarning("*** failed sending server info request");
		return -1;
	}
	delete[] packet;
	
	connectionState = INFO;
	return 0;
}
