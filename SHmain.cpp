//SH Exchange Data
//update-time 20200623
//最终版

//////Header file
//system
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <json/json.h>

#include <iostream>
#include <string>
#include <cassert>
#include <memory>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <chrono>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <ctime>
#include <vector>
#include <map>

//FAST protocol
#include <mfast.h>
#include <mfast/coder/fast_decoder.h>
#include <mfast/json/json.h>
#include <mfast/xml_parser/dynamic_templates_description.h>

//CedarTools
#include <cedar/NewCedarConfig.h>

//STEP protocol
#include <STEP/UserLogonRequest.h>
#include <STEP/UserStaticRequest.h>
#include <StepSetup.h>

//local
#include <include/folder_tool.h>
#include <include/main_struct.h>
#include <include/time_tool.h>


//////////////////////////////////////////////////////////////////全局变量////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//config																												
char ip[16];
int port;
std::string p_ProtocolEdition;
std::string p_SenderCompID;
std::string p_TragetCompID;
int p_EbcryptMethod;
int p_HeartBtInt;
std::string l_template_path;
std::string l_fast_template;
std::string export_folder;

//上海交易所代码
int exchCode = 1;

//socket buffer length
//分配给上证15MB
int socket_buffer_length = 15 * 1024 * 1024;//15MB

//backup
//信号量
sem_t close_mutex;
//marketmanager 对象 g_ 全局
MarketManager g_MarketManager;

//文件流，类型为FILE*
FILE* SH_Quote_File = NULL;
FILE* SH_Trade_File = NULL;
FILE* SH_Index_File = NULL;

//Counter 计数器
static int QuoteCount = 0;
static int TradeCount = 0;
static int IndexCount = 0;

//write counter 写计数器
static int writeQuoteCount = 0;
static int writeTradeCount = 0;
static int writeIndexCount = 0;

//时间变量
// timval 对象 成员对象有sec 和 微秒
struct timeval tv;

//struct timezone结构体
struct timezone tz;

// tm 对象，可以获取日期和时间
struct tm* t;

//Mutex std::mutex 锁
mutex Quote_mutex;
mutex Trade_mutex;
mutex Index_mutex;

//message 消息长度
int MAX_LEN = 200000;

dataTick		g_Quote[200000];
dataTransaction g_Trade[200000];
dataIndex		g_Index[200000];

//fflush times
// 冲洗流中信息的时间间隔
int quote_fflush = 2000;
int index_fflush = 2000;
int trade_fflush = 2000;

//Thread
//进程 设置进程 函数
bool g_exit_thread = false;
void* saveQuoteIntoFiles(void* arg);
void* saveTradeIntoFiles(void* arg);
void* saveIndexIntoFiles(void* arg);



//Json 对象 每组分别对应一个reader 一个 val
// 共计 quote，Index和trde 三组
Json::Reader QuoteReader;
Json::Value QuoteVal;
Json::Reader IndexReader;
Json::Value IndexVal;
Json::Reader TradeReader;
Json::Value TradeVal;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////使用mfast命名空间//////////////////////////////////////////////////////////
//mfast
using mfast::templates_description;
using mfast::dynamic_templates_description;
using mfast::fast_decoder;
using mfast::message_cref;
using mfast::ascii_string_cref;
using mfast::json::encode;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////声明要使用的time_tool.h 函数////////////////////////////////////////////////
//Time tool
unsigned int GetTodayDate();
unsigned int GetTodayTime();
long GetCurrentTimeMsec_my();
unsigned long long GetCurrentTimeMsec();
unsigned long long ServerTimeToMsec(unsigned long server_date, unsigned long server_time);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////声明要使用的folder_tool.h 函数/////////////////////////////////////////////
//Folder tool
bool create_folder(const std::string& folder_path);
bool folder_exist(const std::string& dir);
void create_folder_my(const std::string& forder_name);

////////////////////////////////////////////////////////////////函数区////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//读取config
void LoadConfig() {
	NewCedarConfig::getInstance().loadConfigFile("/home/sysop/ExchangeData/SH/config/SHData_Cedar.cfg");
	strcpy(ip, ConfigGetStringValue("BASE.IP").c_str());
	port = atoi(ConfigGetStringValue("BASE.Port").c_str());
	p_ProtocolEdition = ConfigGetStringValue("BASE.ProtocolEdition");
	p_SenderCompID = ConfigGetStringValue("BASE.SenderCompID");
	p_TragetCompID = ConfigGetStringValue("BASE.TragetCompID");
	p_EbcryptMethod = atoi(ConfigGetStringValue("BASE.EbcryptMethod").c_str());
	p_HeartBtInt = atoi(ConfigGetStringValue("BASE.HeartBtInt").c_str());
	l_template_path = ConfigGetStringValue("BASE.Template");
	export_folder = ConfigGetStringValue("BASE.Path").c_str();
}

//check and create folder
void CreateBaseFolder() {
	std::string day = std::to_string(GetTodayDate());
	unsigned int timeNow = GetTodayTime();
	std::string path = export_folder;

	create_folder_my(path + "Trade/");
	create_folder_my(path + "Quote/");
	create_folder_my(path + "Index/");

	SH_Quote_File = fopen((path + "Quote/" + day + "_Quote_SH.csv").c_str(), "ab");
	SH_Trade_File = fopen((path + "Trade/" + day + "_Trade_SH.csv").c_str(), "ab");
	SH_Index_File = fopen((path + "Index/" + day + "_Index_SH.csv").c_str(), "ab");

	//当前时间小于9:15分时，会
	if (timeNow < 91500)
	{
		fprintf(SH_Quote_File, "InstrumentID,Date,DATATIMESTAMP,TradingPhase,HIGHLIMIT,LOWLIMIT,PRECLOSE,NUMTRADES,TOTALVOLUME,TURNOVER,LASTPX,OPENPX,HIGHPX,LOWPX,TOTALBIDQTY,TOTALOFFERQTY,WAVGBIDPX,WAVGOFFERPX,WithdrawBidNo,WithdrawBidVol,WithdrawBidAmount,WithdrawAskNo,WithdrawAskVol,WithdrawAskAmount,BuyNumber,SellNumber,BuyTradeMaxDuration,SellTradeMaxDuration,NumBidOrders,NumOfferOrders,B01,BV01,BC01,S01,SV01,SC01,B02,BV02,BC02,S02,SV02,SC02,B03,BV03,BC03,S03,SV03,SC03,B04,BV04,BC04,S04,SV04,SC04,B05,BV05,BC05,S05,SV05,SC05,B06,BV06,BC06,S06,SV06,SC06,B07,BV07,BC07,S07,SV07,SC07,B08,BV08,BC08,S08,SV08,SC08,B09,BV09,BC09,S09,SV09,SC09,B10,BV10,BC10,S10,SV10,SC10\n");
		fprintf(SH_Trade_File, "InstrumentID,Date,Time,TradeIndex,BuyIndex,SellIndex,TradeType,BSFlag,Price,Volume\n");
		fprintf(SH_Index_File, "InstrumentID,Date,Time,LocalTime,DelayMS,ExchCode,TradingPhase,HIGHLIMIT,LOWLIMIT,PRECLOSE,NUMTRADES,TOTALVOLUME,TURNOVER,LASTPX,OPENPX,HIGHPX,LOWPX\n");
		fflush(SH_Quote_File);
		fflush(SH_Trade_File);
		fflush(SH_Index_File);
	}



}



//封装静态报文
std::string SetStaticRequest(std::string p_ProtocolEdition, std::string p_SenderCompID,
	std::string p_TragetCompID,int p_RebuildMethod, int p_CategoryID, int p_BeginID, int p_EndID, int checkcode)
{

	cout << "Static rebuild" << endl;

	STEP::UserStaticRequest StaticRequest;

	//BeginString, tag = 8;
	StaticRequest.getHeader().set(FIX::BeginString(p_ProtocolEdition));
	//BodyLength, tag = 9;
	//MsgType, tag = 35;
	//SenderCompID, tag = 49;
	StaticRequest.getHeader().set(FIX::SenderCompID(p_SenderCompID)); //提供
	//TargetCompID, tag = 56;
	StaticRequest.getHeader().set(FIX::TargetCompID(p_TragetCompID));
	//MsgSeqNum, tag = 34;
	StaticRequest.getHeader().set(FIX::MsgSeqNum(checkcode));
	//SendingTime, tag = 52;
	FIX::UtcTimeStamp TimeNow;
	gettimeofday(&tv, &tz);
	t = localtime(&tv.tv_sec);
	TimeNow.setYMD(1900 + t->tm_year, 1 + t->tm_mon, t->tm_mday);
	TimeNow.setHMS(t->tm_hour, t->tm_min, t->tm_sec, tv.tv_usec);
	StaticRequest.getHeader().set(FIX::SendingTime(TimeNow));
	//EncryptMethod, tag = 98;
	//LogonRequest.set(FIX::EncryptMethod(p_EbcryptMethod));
	//HeartBtInt, tag = 108;
	//LogonRequest.set(FIX::HeartBtInt(p_HeartBtInt));
	//CheckSum = 10;
	StaticRequest.set(FIX::RebuildMethod(p_RebuildMethod));
	StaticRequest.set(FIX::CategoryID(p_CategoryID));
	StaticRequest.set(FIX::BeginID(p_BeginID));
	StaticRequest.set(FIX::EndID(p_EndID));

	std::string StaticRequest_str;
	StaticRequest.toString(StaticRequest_str);



	return StaticRequest_str;

}




//封装登录报文
std::string SetLogonRequest(std::string p_ProtocolEdition, std::string p_SenderCompID,
	std::string p_TragetCompID, int p_EbcryptMethod, int p_HeartBtInt, int checkcode)
{

	cout << "logon" << endl;

	STEP::UserLogonRequest LogonRequest;

	//BeginString, tag = 8;
	LogonRequest.getHeader().set(FIX::BeginString(p_ProtocolEdition));
	//BodyLength, tag = 9;
	//MsgType, tag = 35;
	//SenderCompID, tag = 49;
	LogonRequest.getHeader().set(FIX::SenderCompID(p_SenderCompID)); //提供
	//TargetCompID, tag = 56;
	LogonRequest.getHeader().set(FIX::TargetCompID(p_TragetCompID));
	//MsgSeqNum, tag = 34;
	LogonRequest.getHeader().set(FIX::MsgSeqNum(checkcode));
	//SendingTime, tag = 52;
	FIX::UtcTimeStamp TimeNow;
	gettimeofday(&tv, &tz);
	t = localtime(&tv.tv_sec);
	TimeNow.setYMD(1900 + t->tm_year, 1 + t->tm_mon, t->tm_mday);
	TimeNow.setHMS(t->tm_hour, t->tm_min, t->tm_sec, tv.tv_usec);
	LogonRequest.getHeader().set(FIX::SendingTime(TimeNow));
	//EncryptMethod, tag = 98;
	LogonRequest.set(FIX::EncryptMethod(p_EbcryptMethod));
	//HeartBtInt, tag = 108;
	LogonRequest.set(FIX::HeartBtInt(p_HeartBtInt));
	//CheckSum = 10;
	std::string LogonRequest_str;
	LogonRequest.toString(LogonRequest_str);
	

	cout<<LogonRequest_str<<endl;
	return LogonRequest_str;

}

//读取template
std::string LoadTemplate(std::string path) {
	std::string fast_template;
	std::fstream template_file(path, std::ios::in);
	if (template_file)
	{

		template_file.seekg(0, std::ios::end);
		size_t len = (size_t)template_file.tellg();
		if (len > 0)
		{
			char* tmp = new char[len];
			template_file.seekg(0, std::ios::beg);
			template_file.read(tmp, len);
			fast_template.assign(tmp, len);
			delete[]tmp;
		}
		else
		{
			std::cout << "template length error" << std::endl;
			template_file.close();
			return 0;
		}
		template_file.close();
	}
	else
	{
		std::cout << "template not exist" << std::endl;
		return 0;
	}

	std::cout << "template loaded! " << std::endl;
	return fast_template;
}

//socket连接
int SocketLogon(std::string request) {
	struct sockaddr_in  servaddr;
	//socket init 
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		printf("create socket error: %s(errno: %d)\n", strerror(errno), errno);
		return 0;
	}
	else {
		printf("socket established\n");
	}

	////////socket设置
	int nRecvBuf = socket_buffer_length;
	int nSendBuf = socket_buffer_length;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&nRecvBuf, sizeof(int));
	setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&nSendBuf, sizeof(int));
	// connection init
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr(ip);
	int connfd = connect(sockfd, (struct sockaddr*) & servaddr, sizeof(servaddr));
	if (connfd < 0) {
		printf("connect error: %s(errno: %d)\n", strerror(errno), errno);
		return 0;
	}
	else {
		printf("connection established\n");
	}
	int logon_flag = send(sockfd, request.c_str(), strlen(request.c_str()), 0);
	printf("send success\n");
	if (logon_flag < 0)
	{
		printf("logon error: %s(errno: %d)\n", strerror(errno), errno);
		return 0;
	}
	else
	{
		printf("logon success\n");
		return sockfd;
	}
}

//拆分fix报文
std::map<std::string, std::string> DecodeTag(std::string msgbuff) {

	int KeyPos = 0;
	std::vector<std::string> dest;
	std::vector<std::string>::iterator iter; //声明一个迭代器
	std::string Insta;
	char soh_t = 1;
	std::string separator = "";
	separator.append(1, soh_t);
	std::string str_S = msgbuff.c_str();

	std::string substring;
	std::string::size_type start = 0, index;
	dest.clear();
	index = str_S.find_first_of(separator, start);
	do
	{
		if (index != std::string::npos)
		{
			substring = str_S.substr(start, index - start);
			dest.push_back(substring);
			start = index + separator.size();
			index = str_S.find(separator, start);
			if (start == std::string::npos) break;
		}
	} while (index != string::npos);

	substring = str_S.substr(start);
	dest.push_back(substring);

	//存放fix解密报文
	std::map<std::string, std::string> StepMsgMap;

	for (iter = dest.begin(); iter != dest.end(); iter++)
	{
		Insta = *iter;
		std::string fix_key;
		std::string fix_value;
		int nPos = Insta.find_last_of('=');
		fix_key = Insta.substr(0, nPos);
		fix_value = Insta.substr(nPos + 1, Insta.length() - nPos);
		KeyPos = KeyPos + strlen(fix_key.c_str()) + 2 + strlen(fix_value.c_str());
		if (atoi(fix_key.c_str()) == 95) {
			char KeyPos_str[3];
			KeyPos = KeyPos + strlen(fix_key.c_str()) + 1;
			sprintf(KeyPos_str, "%d", KeyPos);
			StepMsgMap.insert(make_pair("KeyPos", KeyPos_str));
		}
		StepMsgMap.insert(make_pair(fix_key, fix_value));
	}
	return StepMsgMap;

}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



//////////////////////////////////////////////////////////////消息接受与处理类///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class MassageSaveHandle : public MarketManager {

public:

	void onMassageData(int l_sockfd,std::string l_fast_template) {

		dynamic_templates_description description(l_fast_template);
		const templates_description* descriptions[] = { &description };
		fast_decoder decoder;
		decoder.include(descriptions);

		unsigned int date_today = GetTodayDate();
		int length;
		int header_length;
		int header_length_main;
		int header_length_remain;
		int header_length_temp;
		int body_length_main;
		int body_length_remain;
		int body_length_temp;
		int count = 0;

		std::map<std::string, std::string> l_StepMsgMap_header;
		std::map<std::string, std::string> l_StepMsgMap_body;

		while (1) {

			count++;
			//循环收取数据头
			char Msgheader[21];
			memset(Msgheader, 0, 21);

			header_length_main = 0;
			while (header_length_main < 21)
			{
				header_length_remain = 21 - header_length_main;
				if (header_length_remain > 0)
				{
					header_length_temp = recv(l_sockfd, Msgheader + header_length_main, header_length_remain, 0);
					header_length_main += header_length_temp;
				}
			}

			l_StepMsgMap_header = DecodeTag(Msgheader);
			length = atoi(l_StepMsgMap_header["9"].c_str())+7;//7位 checksum

			char Msgbuff[length];
			memset(Msgbuff, 0, length);

			body_length_main = 0;
			if (Msgheader[19] == 0x01)
			{
				body_length_main = 1;
				//length = length - 1;
				Msgbuff[0] = Msgheader[20];
			}
			if (Msgheader[18] == 0x01)
			{
				body_length_main = 2;
				//length = length - 2;
				Msgbuff[0] = Msgheader[19];
				Msgbuff[1] = Msgheader[20];
			}
			if (Msgheader[17] == 0x01)
			{
				body_length_main = 3;
				//length = length - 3;
				Msgbuff[0] = Msgheader[18];
				Msgbuff[1] = Msgheader[19];
				Msgbuff[2] = Msgheader[20];
			}
			


			//body_length_main = 0;
			while (body_length_main < length)
			{
				body_length_remain = length - body_length_main;
				if (body_length_remain > 0)
				{
					body_length_temp = recv(l_sockfd, Msgbuff + body_length_main, body_length_remain, 0);
					body_length_main += body_length_temp;
				}
			}

			


			l_StepMsgMap_body = DecodeTag(Msgbuff);

			switch (MsgTypeMap[l_StepMsgMap_body["35"]])
			{
			////已接入数据
			//quote
			case UA3202://level-2 竞价行情数据
			{
				const char* msg_start = &Msgbuff[0] + atoi(l_StepMsgMap_body["KeyPos"].c_str());
				const char* msg_end = msg_start + atoi(l_StepMsgMap_body["95"].c_str());

				for (size_t i = 1; msg_start != msg_end; ++i)
				{
					Quote_mutex.lock();
					int accessPosi = QuoteCount % MAX_LEN;

					message_cref quote_msg = decoder.decode(msg_start, msg_end, false);
					std::ostringstream quote_json_message;
					bool result = encode(quote_json_message, quote_msg, 0);

					if (result)
					{
						//std::cout << "Success: " << std::endl << quote_json_message.str() << std::endl << std::endl;

						QuoteReader.parse(quote_json_message.str(), QuoteVal);
						


						g_Quote[accessPosi].exchangeCode = exchCode;
						strncpy(g_Quote[accessPosi].securityId, QuoteVal["SecurityID"].asString().c_str(), 6);
						//g_Quote[accessPosi].localtime = 0;
						g_Quote[accessPosi].time = QuoteVal["DataTimeStamp"].asInt();
						g_Quote[accessPosi].date = date_today;
						g_Quote[accessPosi].delayTime = 0;
						strcpy(g_Quote[accessPosi].tradingPhaseCode, QuoteVal["TradingPhaseCode"].asString().c_str());

						//g_Quote[accessPosi].maxPx = stock.maxpx();
						//g_Quote[accessPosi].minPx = stock.minpx();


						g_Quote[accessPosi].preClosePx = QuoteVal["PreClosePx"].asInt();
						g_Quote[accessPosi].numTrades = QuoteVal["NumTrades"].asInt64();
						g_Quote[accessPosi].totalVolumeTrade = QuoteVal["TotalVolumeTrade"].asInt64();
						g_Quote[accessPosi].totalValueTrade = QuoteVal["TotalValueTrade"].asInt64();
						g_Quote[accessPosi].lastPx = QuoteVal["LastPx"].asInt();
						g_Quote[accessPosi].openPx = QuoteVal["OpenPx"].asInt();
						g_Quote[accessPosi].highPx = QuoteVal["HighPx"].asInt();
						g_Quote[accessPosi].lowPx = QuoteVal["LowPx"].asInt();
						g_Quote[accessPosi].totalBuyQty = QuoteVal["TotalBidQty"].asInt64();
						g_Quote[accessPosi].totalSellQty = QuoteVal["TotalOfferQty"].asInt64();
						g_Quote[accessPosi].weightedAvgBuyPx = QuoteVal["WeightedAvgBidPx"].asInt();
						g_Quote[accessPosi].weightedAvgSellPx = QuoteVal["WeightedAvgOfferPx"].asInt();
						g_Quote[accessPosi].buyTradeMaxDuration = QuoteVal["BidTradeMaxDuration"].asInt();
						g_Quote[accessPosi].sellTradeMaxDuration = QuoteVal["OfferTradeMaxDuration"].asInt();
						g_Quote[accessPosi].withdrawBuyNumber = QuoteVal["WithdrawBuyNumber"].asInt();
						g_Quote[accessPosi].withdrawBuyAmount = QuoteVal["WithdrawBuyAmount"].asInt64();
						g_Quote[accessPosi].withdrawBuyMoney = QuoteVal["WithdrawBuyMoney"].asInt64();
						g_Quote[accessPosi].withdrawSellNumber = QuoteVal["WithdrawSellNumber"].asInt();
						g_Quote[accessPosi].withdrawSellAmount = QuoteVal["WithdrawSellAmount"].asInt64();
						g_Quote[accessPosi].withdrawSellMoney = QuoteVal["WithdrawSellMoney"].asInt64();
						g_Quote[accessPosi].totalBuyNumber = QuoteVal["TotalBidNumber"].asInt();
						g_Quote[accessPosi].totalSellNumber = QuoteVal["TotalOfferNumber"].asInt();
						g_Quote[accessPosi].numBuyOrders = QuoteVal["NumBidOrders"].asInt();
						g_Quote[accessPosi].numSellOrders = QuoteVal["NumOfferOrders "].asInt();


						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].buyPx[i] = QuoteVal["BidLevels"][i]["Price"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].buyVol[i] = QuoteVal["BidLevels"][i]["OrderQty"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].buyCnt[i] = QuoteVal["BidLevels"][i]["NumOrders"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].sellPx[i] = QuoteVal["OfferLevels"][i]["Price"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].sellVol[i] = QuoteVal["OfferLevels"][i]["OrderQty"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].sellCnt[i] = QuoteVal["OfferLevels"][i]["NumOrders"].asInt64();
						}



						
						g_Quote[accessPosi].NumMissedBidTrades = 0;
						g_Quote[accessPosi].level1BidQueueCount = 0;
						g_Quote[accessPosi].level1AskQueueCount = 0;
						g_Quote[accessPosi].shortsellsharestraded = 0;
						g_Quote[accessPosi].shortsellturnover = 0;

						g_Quote[accessPosi].level1BidQueueCount = 0;
						g_Quote[accessPosi].level1AskQueueCount = 0;
						g_Quote[accessPosi].shortsellsharestraded = 0;
						g_Quote[accessPosi].shortsellturnover = 0;
						
					}

					__sync_fetch_and_add(&QuoteCount, 1);
					Quote_mutex.unlock();
				}


				l_StepMsgMap_header.erase(l_StepMsgMap_header.begin(), l_StepMsgMap_header.end());
				l_StepMsgMap_body.erase(l_StepMsgMap_body.begin(), l_StepMsgMap_body.end());

				
				break;
			}

			case UA3108://level-2 盘后固定价格交易行情数据
			{ 
				const char* msg_start = &Msgbuff[0] + atoi(l_StepMsgMap_body["KeyPos"].c_str());
				const char* msg_end = msg_start + atoi(l_StepMsgMap_body["95"].c_str());

				for (size_t i = 1; msg_start != msg_end; ++i)
				{
					Quote_mutex.lock();
					int accessPosi = QuoteCount % MAX_LEN;

					message_cref quote_msg = decoder.decode(msg_start, msg_end, false);
					std::ostringstream quote_json_message;
					bool result = encode(quote_json_message, quote_msg, 0);

					if (result)
					{
						//std::cout << "Success: " << std::endl << quote_json_message.str() << std::endl << std::endl;

						QuoteReader.parse(quote_json_message.str(), QuoteVal);


						g_Quote[accessPosi].exchangeCode = exchCode;
						strncpy(g_Quote[accessPosi].securityId, QuoteVal["SecurityID"].asString().c_str(), 6);
						//g_Quote[accessPosi].localtime = 0;
						g_Quote[accessPosi].time = QuoteVal["DataTimeStamp"].asInt();
						g_Quote[accessPosi].date = date_today;
						g_Quote[accessPosi].delayTime = 0;
						strcpy(g_Quote[accessPosi].tradingPhaseCode, QuoteVal["TradingPhaseCode"].asString().c_str());

						//g_Quote[accessPosi].maxPx = stock.maxpx();
						//g_Quote[accessPosi].minPx = stock.minpx();


						g_Quote[accessPosi].preClosePx = QuoteVal["PreClosePx"].asInt();
						g_Quote[accessPosi].numTrades = QuoteVal["NumTrades"].asInt64();
						g_Quote[accessPosi].totalVolumeTrade = QuoteVal["TotalVolumeTrade"].asInt64();
						g_Quote[accessPosi].totalValueTrade = QuoteVal["TotalValueTrade"].asInt64();
						g_Quote[accessPosi].lastPx = QuoteVal["LastPx"].asInt();
						g_Quote[accessPosi].openPx = QuoteVal["OpenPx"].asInt();
						g_Quote[accessPosi].highPx = QuoteVal["HighPx"].asInt();
						g_Quote[accessPosi].lowPx = QuoteVal["LowPx"].asInt();
						g_Quote[accessPosi].totalBuyQty = QuoteVal["TotalBidQty"].asInt64();
						g_Quote[accessPosi].totalSellQty = QuoteVal["TotalOfferQty"].asInt64();
						g_Quote[accessPosi].weightedAvgBuyPx = QuoteVal["WeightedAvgBidPx"].asInt();
						g_Quote[accessPosi].weightedAvgSellPx = QuoteVal["WeightedAvgOfferPx"].asInt();
						g_Quote[accessPosi].buyTradeMaxDuration = QuoteVal["BidTradeMaxDuration"].asInt();
						g_Quote[accessPosi].sellTradeMaxDuration = QuoteVal["OfferTradeMaxDuration"].asInt();
						g_Quote[accessPosi].withdrawBuyNumber = QuoteVal["WithdrawBuyNumber"].asInt();
						g_Quote[accessPosi].withdrawBuyAmount = QuoteVal["WithdrawBuyAmount"].asInt64();
						g_Quote[accessPosi].withdrawBuyMoney = QuoteVal["WithdrawBuyMoney"].asInt64();
						g_Quote[accessPosi].withdrawSellNumber = QuoteVal["WithdrawSellNumber"].asInt();
						g_Quote[accessPosi].withdrawSellAmount = QuoteVal["WithdrawSellAmount"].asInt64();
						g_Quote[accessPosi].withdrawSellMoney = QuoteVal["WithdrawSellMoney"].asInt64();
						g_Quote[accessPosi].totalBuyNumber = QuoteVal["TotalBidNumber"].asInt();
						g_Quote[accessPosi].totalSellNumber = QuoteVal["TotalOfferNumber"].asInt();
						g_Quote[accessPosi].numBuyOrders = QuoteVal["NumBidOrders"].asInt();
						g_Quote[accessPosi].numSellOrders = QuoteVal["NumOfferOrders "].asInt();


						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].buyPx[i] = QuoteVal["BidLevels"][i]["Price"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].buyVol[i] = QuoteVal["BidLevels"][i]["OrderQty"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].buyCnt[i] = QuoteVal["BidLevels"][i]["NumOrders"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].sellPx[i] = QuoteVal["OfferLevels"][i]["Price"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].sellVol[i] = QuoteVal["OfferLevels"][i]["OrderQty"].asInt64();
						}

						for (int i = 0; i < 10; i++) {
							g_Quote[accessPosi].sellCnt[i] = QuoteVal["OfferLevels"][i]["NumOrders"].asInt64();
						}




						g_Quote[accessPosi].NumMissedBidTrades = 0;
						g_Quote[accessPosi].level1BidQueueCount = 0;
						g_Quote[accessPosi].level1AskQueueCount = 0;
						g_Quote[accessPosi].shortsellsharestraded = 0;
						g_Quote[accessPosi].shortsellturnover = 0;

						g_Quote[accessPosi].level1BidQueueCount = 0;
						g_Quote[accessPosi].level1AskQueueCount = 0;
						g_Quote[accessPosi].shortsellsharestraded = 0;
						g_Quote[accessPosi].shortsellturnover = 0;

					}

					__sync_fetch_and_add(&QuoteCount, 1);
					Quote_mutex.unlock();
				}

				l_StepMsgMap_header.erase(l_StepMsgMap_header.begin(), l_StepMsgMap_header.end());
				l_StepMsgMap_body.erase(l_StepMsgMap_body.begin(), l_StepMsgMap_body.end());

				break;
			}

			//index
			case UA3113://level-2 指数行情数据
			{
				const char* msg_start = &Msgbuff[0] + atoi(l_StepMsgMap_body["KeyPos"].c_str());
				const char* msg_end = msg_start + atoi(l_StepMsgMap_body["95"].c_str());

				for (size_t i = 1; msg_start != msg_end; ++i)
				{
					Index_mutex.lock();
					int accessPosi = QuoteCount % MAX_LEN;

					message_cref index_msg = decoder.decode(msg_start, msg_end, false);
					std::ostringstream index_json_message;
					bool result = encode(index_json_message, index_msg, 0);

					if (result)
					{
						IndexReader.parse(index_json_message.str(), IndexVal);

						strncpy(g_Index[accessPosi].securityId, IndexVal["SecurityID"].asString().c_str(),6);
						g_Index[accessPosi].date = date_today;
						g_Index[accessPosi].time = IndexVal["DataTimeStamp"].asInt();
						g_Index[accessPosi].exchangeCode = exchCode;
						g_Index[accessPosi].preClosePx = IndexVal["PreCloseIndex"].asInt64();
						g_Index[accessPosi].totalVolumeTrade = IndexVal["TotalVolumeTraded"].asInt64();
						g_Index[accessPosi].totalValueTrade = IndexVal["Turnover"].asInt64();
						g_Index[accessPosi].lastPx = IndexVal["LastIndex"].asInt64();
						g_Index[accessPosi].openPx = IndexVal["OpenIndex"].asInt64();
						g_Index[accessPosi].highPx = IndexVal["HighIndex"].asInt64();
						g_Index[accessPosi].lowPx = IndexVal["LowIndex"].asInt64();
					}

					__sync_fetch_and_add(&IndexCount, 1);
					Index_mutex.unlock();
				}

				l_StepMsgMap_header.erase(l_StepMsgMap_header.begin(), l_StepMsgMap_header.end());
				l_StepMsgMap_body.erase(l_StepMsgMap_body.begin(), l_StepMsgMap_body.end());

				break;
			}

			//trade
			case UA3201://level-2 竞价逐笔成交数据
			{
				const char* msg_start = &Msgbuff[0] + atoi(l_StepMsgMap_body["KeyPos"].c_str());
				const char* msg_end = msg_start + atoi(l_StepMsgMap_body["95"].c_str());

				for (size_t i = 1; msg_start != msg_end; ++i)
				{
					Trade_mutex.lock();
					int accessPosi = TradeCount % MAX_LEN;

					message_cref trade_msg = decoder.decode(msg_start, msg_end, false);
					std::ostringstream trade_json_message;
					bool result = encode(trade_json_message, trade_msg, 0);

					if (result)
					{
						TradeReader.parse(trade_json_message.str(), TradeVal);
						g_Trade[accessPosi].exchangeCode = exchCode;
						strncpy(g_Trade[accessPosi].securityId, TradeVal["SecurityID"].asString().c_str(), 6);
						g_Trade[accessPosi].time = TradeVal["TradeTime"].asInt();
						g_Trade[accessPosi].date = date_today;
						g_Trade[accessPosi].tradeIndex = TradeVal["TradeIndex"].asInt();
						g_Trade[accessPosi].tradeBuyNo = TradeVal["TradeBuyNo"].asInt64();
						g_Trade[accessPosi].tradeSellNo = TradeVal["TradeSellNo"].asInt64();
						g_Trade[accessPosi].tradeBsFlag = TradeBSflag[TradeVal["TradeBSFlag"].asString().c_str()];
						g_Trade[accessPosi].tradePx = TradeVal["TradePrice"].asInt();
						g_Trade[accessPosi].tradeQty = TradeVal["TradeQty"].asInt64();


						OrderBook* pbook = g_MarketManager.FindOrderBook(g_Trade[accessPosi].securityId);
						pbook->onTrade(&g_Trade[accessPosi]);
						g_MarketManager.ReleaseMarketManager();

					}

					__sync_fetch_and_add(&TradeCount, 1);
					Trade_mutex.unlock();
				}

				l_StepMsgMap_header.erase(l_StepMsgMap_header.begin(), l_StepMsgMap_header.end());
				l_StepMsgMap_body.erase(l_StepMsgMap_body.begin(), l_StepMsgMap_body.end());
				
				break;
			}

			case UA3209://level-2 盘后固定价格交易逐笔成交数据
			{
				const char* msg_start = &Msgbuff[0] + atoi(l_StepMsgMap_body["KeyPos"].c_str());
				const char* msg_end = msg_start + atoi(l_StepMsgMap_body["95"].c_str());

				for (size_t i = 1; msg_start != msg_end; ++i)
				{
					Trade_mutex.lock();
					int accessPosi = TradeCount % MAX_LEN;

					message_cref trade_msg = decoder.decode(msg_start, msg_end, false);
					std::ostringstream trade_json_message;
					bool result = encode(trade_json_message, trade_msg, 0);

					if (result)
					{
						TradeReader.parse(trade_json_message.str(), TradeVal);

						g_Trade[accessPosi].exchangeCode = exchCode;
						strncpy(g_Trade[accessPosi].securityId, TradeVal["SecurityID"].asString().c_str(), 6);
						g_Trade[accessPosi].time = TradeVal["TradeTime"].asInt();
						g_Trade[accessPosi].date = date_today;
						g_Trade[accessPosi].tradeIndex = TradeVal["TradeIndex"].asInt();
						g_Trade[accessPosi].tradeBuyNo = TradeVal["TradeBuyNo"].asInt64();
						g_Trade[accessPosi].tradeSellNo = TradeVal["TradeSellNo"].asInt64();
						g_Trade[accessPosi].tradeBsFlag = TradeBSflag[TradeVal["TradeBSFlag"].asString().c_str()];
						g_Trade[accessPosi].tradePx = TradeVal["TradePrice"].asInt();
						g_Trade[accessPosi].tradeQty = TradeVal["TradeQty"].asInt64();


						OrderBook* pbook = g_MarketManager.FindOrderBook(g_Trade[accessPosi].securityId);
						pbook->onTrade(&g_Trade[accessPosi]);
						g_MarketManager.ReleaseMarketManager();
					}
					__sync_fetch_and_add(&TradeCount, 1);
					Trade_mutex.unlock();
				}

				l_StepMsgMap_header.erase(l_StepMsgMap_header.begin(), l_StepMsgMap_header.end());
				l_StepMsgMap_body.erase(l_StepMsgMap_body.begin(), l_StepMsgMap_body.end());

				break;
			}

			////未接入数据，需要接入时请检查
			case A://登录消息
			{
				break;
			}
			case 5://退出消息
			{
				break;
			}
			case UA1202://系统心跳消息
			{
				break;
			}
			case UA1201://请求静态数据及数据重建消息
			{
				for (int i = 0; i < 21; i++) {
					printf("%02x|", Msgheader[i]);
				}
				std::cout << std::endl;
				for (int i = 0; i < length; i++) {
					printf("%02x|", Msgbuff[i]);
				}
				std::cout << std::endl;
				std::cout << "BeginString:" << l_StepMsgMap_header["8"] << std::endl;
				std::cout << "BodyLength:" << l_StepMsgMap_header["9"] << std::endl;
				std::cout << "MsgType:" << l_StepMsgMap_body["35"] << std::endl;
				std::cout << "SenderCompID:" << l_StepMsgMap_body["49"] << std::endl;
				std::cout << "TargetCompID:" << l_StepMsgMap_body["56"] << std::endl;
				std::cout << "MsgSeqNum:" << l_StepMsgMap_body["34"] << std::endl;
				std::cout << "SendingTime:" << l_StepMsgMap_body["52"] << std::endl;
				std::cout << "MessageEncoding:" << l_StepMsgMap_body["347"] << std::endl;
				std::cout << "SourceId:" << l_StepMsgMap_body["50"] << std::endl;
				std::cout << "CategoryID:" << l_StepMsgMap_body["10142"] << std::endl;
				std::cout << "MsgSeqID:" << l_StepMsgMap_body["10072"] << std::endl;
				std::cout << "RawDataLength:" << l_StepMsgMap_body["95"] << std::endl;
				std::cout << "KEYPOS:" << l_StepMsgMap_body["KeyPos"] << std::endl;
				break;
			}
			case UA5302://上交所Level-1 FAST行情,level-1 数据
			{
				break;
			}
			case W:	//证券行情消息
			{
				break;
			}
			case h:	//市场状态消息
			{
				break;
			}
			case UA3115://level-2 市场总览数据
			{
				break;
			}
			////无主信息--需要验证，必要时需要更新template
			case UA7301:
			{
				break;
			}
			case UA2201:
			{

				break;
			}
			case UA1701:
			{

				break;
			}
			case UA1200:
			{

				break;
			}
			case UA12001:
			{

				break;
			}
			////////////////////////////////当所有case 都不满足时///////////////////////////////////////////////////
			default:
			{
				//std::cout << "this is a wild massage!" << std::endl;
				for (int i = 0; i < 21; i++) {
					printf("%02x|", Msgheader[i]);
				}
				std::cout << std::endl;

				for (int i = 0; i < length; i++) {
					printf("%02x|", Msgbuff[i]);
				}
				std::cout << std::endl;


				std::cout << "BeginString:" << l_StepMsgMap_header["8"] << std::endl;
				std::cout << "BodyLength:" << l_StepMsgMap_header["9"] << std::endl;
				std::cout << "MsgType:" << l_StepMsgMap_body["35"] << std::endl;
				std::cout << "SenderCompID:" << l_StepMsgMap_body["49"] << std::endl;
				std::cout << "TargetCompID:" << l_StepMsgMap_body["56"] << std::endl;
				std::cout << "MsgSeqNum:" << l_StepMsgMap_body["34"] << std::endl;
				std::cout << "SendingTime:" << l_StepMsgMap_body["52"] << std::endl;
				std::cout << "MessageEncoding:" << l_StepMsgMap_body["347"] << std::endl;
				std::cout << "SourceId:" << l_StepMsgMap_body["50"] << std::endl;
				std::cout << "CategoryID:" << l_StepMsgMap_body["10142"] << std::endl;
				std::cout << "MsgSeqID:" << l_StepMsgMap_body["10072"] << std::endl;
				std::cout << "RawDataLength:" << l_StepMsgMap_body["95"] << std::endl;
				std::cout << "KEYPOS:" << l_StepMsgMap_body["KeyPos"] << std::endl;

				break;
			}

			/*调试工具
			for (int i = 0; i < 21; i++) {
				printf("%02x|", Msgheader[i]);
			}
			std::cout << std::endl;

			for (int i = 0; i < length; i++) {
				printf("%02x|", Msgbuff[i]);
			}
			std::cout << std::endl;
			std::cout << "BeginString:" << l_StepMsgMap_header["8"] << std::endl;
			std::cout << "BodyLength:" << l_StepMsgMap_header["9"] << std::endl;
			std::cout << "MsgType:" << l_StepMsgMap_body["35"] << std::endl;
			std::cout << "SenderCompID:" << l_StepMsgMap_body["49"] << std::endl;
			std::cout << "TargetCompID:" << l_StepMsgMap_body["56"] << std::endl;
			std::cout << "MsgSeqNum:" << l_StepMsgMap_body["34"] << std::endl;
			std::cout << "SendingTime:" << l_StepMsgMap_body["52"] << std::endl;
			std::cout << "MessageEncoding:" << l_StepMsgMap_body["347"] << std::endl;
			std::cout << "SourceId:" << l_StepMsgMap_body["50"] << std::endl;
			std::cout << "CategoryID:" << l_StepMsgMap_body["10142"] << std::endl;
			std::cout << "MsgSeqID:" << l_StepMsgMap_body["10072"] << std::endl;
			std::cout << "RawDataLength:" << l_StepMsgMap_body["95"] << std::endl;
			std::cout << "KEYPOS:" << l_StepMsgMap_body["KeyPos"] << std::endl;

			*/
		

			}

		}

	}
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////// 	进程区		///////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//datahandout 函数
//该函数负责 创建三个进程，三个进程分别负责 读取 Quote,Trade 和Index数据
// 三个函数指针都已经在全局变量中定义
void DataHandOut(int l_sockfd,std::string l_template_path) {

	void* (*psaveQuoteFunc) (void* arg);
	void* (*psaveTradeFunc) (void* arg);
	void* (*psaveIndexFunc) (void* arg);

	psaveQuoteFunc = saveQuoteIntoFiles;
	psaveTradeFunc = saveTradeIntoFiles;
	psaveIndexFunc = saveIndexIntoFiles;


	//创建 Quote 进程
	pthread_t saveQuoteThread;
	if ((pthread_create(&saveQuoteThread, NULL, psaveQuoteFunc, NULL)) !=0)
	{
		printf("create error!\n");
		return;
	}

	//创建 Trade 进程
	pthread_t saveTradeThread;
	if ((pthread_create(&saveTradeThread, NULL, psaveTradeFunc, NULL)) !=0)
	{
		printf("create error!\n");
		return;
	}

	//创建 Index 进程
	pthread_t saveIndexThread;
	if ((pthread_create(&saveIndexThread, NULL, psaveIndexFunc, NULL)) !=0)
	{
		printf("create error!\n");
		return;
	}


	//keep listening

	//创建一个 MessageSaveHandle 对象
	MassageSaveHandle l_MassageSaveHandler;

	//调用对象的 onMassageData函数监听
	l_MassageSaveHandler.onMassageData(l_sockfd, l_fast_template);

	//初始化信号量，初始值为1
	sem_init(&close_mutex,0,1);

	std::cout << "input any key to quit...>>";

	//信号量减1
	//由于信号量减为0,会阻塞进程
	sem_wait(&close_mutex);
	printf("There is a close_program mutex received\n");

	/// end program sequence
	g_exit_thread = true;

	fclose(SH_Quote_File);
	fclose(SH_Trade_File);
	fclose(SH_Index_File);

	//销毁信号量
	sem_destroy(&close_mutex);

	//回收进程空间
	pthread_join(saveQuoteThread, NULL);
	pthread_join(saveTradeThread, NULL);
	pthread_join(saveIndexThread, NULL);

	//close socket
	printf("Program ending now !\n");
	close(l_sockfd);

}

//quote
//quote 数据读取使用的函数指针

void* saveQuoteIntoFiles(void* arg) 
{
	int writeAccess = -1;
	while (1) {

		//writeAccess 是个index，需要循环利用该index
		writeAccess = writeQuoteCount % MAX_LEN;

		//如果读取的数据记数大于了实际发过来的记数，不做事务
		if (writeQuoteCount >= QuoteCount)
		{
			continue;
		}

		//只读取 60和68开头的 Quote，别的直接跳过
		//if ((strncmp(g_Quote[writeAccess].securityId, "60", 2) * strncmp(g_Quote[writeAccess].securityId, "68", 2)) != 0)
		//{
		//	continue;
		//}
		Quote_mutex.lock();


		int tp_code = TradingPhaseMap[g_Quote[writeAccess].tradingPhaseCode[0]];

		if (tp_code == -1) {
			if (g_Quote[writeAccess].time * 1000 < 93000000) {
				tp_code = 2;
			}
			else {
				tp_code = 4;
			}

		}

		//输出到 SH_Quote
		fprintf(SH_Quote_File, "%6.6s.SH,%ld,%ld,%d,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%ld,%ld,",
			g_Quote[writeAccess].securityId,
			g_Quote[writeAccess].date,
			g_Quote[writeAccess].time*1000,
			tp_code,
			g_Quote[writeAccess].maxPx,//--
			g_Quote[writeAccess].minPx,//--
			g_Quote[writeAccess].preClosePx,//--
			g_Quote[writeAccess].numTrades,
			g_Quote[writeAccess].totalVolumeTrade / 1000,
			int(g_Quote[writeAccess].totalValueTrade / 100000),
			g_Quote[writeAccess].lastPx * 10,
			g_Quote[writeAccess].openPx * 10,
			g_Quote[writeAccess].highPx * 10,
			g_Quote[writeAccess].lowPx * 10,
			g_Quote[writeAccess].totalBuyQty / 1000,
			g_Quote[writeAccess].totalSellQty / 1000,
			g_Quote[writeAccess].weightedAvgBuyPx * 10,
			g_Quote[writeAccess].weightedAvgSellPx * 10,
			g_Quote[writeAccess].withdrawBuyNumber,
			g_Quote[writeAccess].withdrawBuyAmount / 1000,
			int(g_Quote[writeAccess].withdrawBuyMoney / 100000),
			g_Quote[writeAccess].withdrawSellNumber,
			g_Quote[writeAccess].withdrawSellAmount / 1000,
			int(g_Quote[writeAccess].withdrawSellMoney / 100000),
			g_Quote[writeAccess].totalBuyNumber,
			g_Quote[writeAccess].totalSellNumber,
			g_Quote[writeAccess].buyTradeMaxDuration,
			g_Quote[writeAccess].sellTradeMaxDuration,
			g_Quote[writeAccess].numBuyOrders,
			g_Quote[writeAccess].numSellOrders);

		for (int i = 0; i < 10; i++) {
			fprintf(SH_Quote_File, "%lld,%lld,%lld,%lld,%lld,%lld,", g_Quote[writeAccess].buyPx[i]*10, g_Quote[writeAccess].buyVol[i]/1000, g_Quote[writeAccess].buyCnt[i], g_Quote[writeAccess].sellPx[i]*10, g_Quote[writeAccess].sellVol[i]/1000, g_Quote[writeAccess].sellCnt[i]);
		}

		fprintf(SH_Quote_File, "\n");

		if (writeQuoteCount % quote_fflush == 0) {
			fflush(SH_Quote_File);
		}
		Quote_mutex.unlock();
		__sync_fetch_and_add(&writeQuoteCount, 1);
	}
	pthread_exit(NULL);

}

//index
void* saveIndexIntoFiles(void* arg)
{
	int writeAccess = -1;
	while (1)
	{
		writeAccess = writeIndexCount % MAX_LEN;
		if (writeIndexCount >= IndexCount)
		{
			continue;
		}
		Index_mutex.lock();


		//输出到SH_Index_File
		fprintf(SH_Index_File, "%s,%ld,%ld,%lld,%d,%d,%s,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld\n",
			g_Index[writeAccess].securityId,
			g_Index[writeAccess].date,
			g_Index[writeAccess].time,
			g_Index[writeAccess].localtime,
			g_Index[writeAccess].delayTime,
			g_Index[writeAccess].exchangeCode,
			g_Index[writeAccess].tradingPhaseCode,
			g_Index[writeAccess].maxPx,
			g_Index[writeAccess].minPx,
			g_Index[writeAccess].preClosePx,
			g_Index[writeAccess].numTrades,
			g_Index[writeAccess].totalVolumeTrade,
			g_Index[writeAccess].totalValueTrade,
			g_Index[writeAccess].lastPx,
			g_Index[writeAccess].openPx,
			g_Index[writeAccess].highPx,
			g_Index[writeAccess].lowPx);

		if (writeIndexCount % index_fflush == 0)
		{
			fflush(SH_Index_File);
		}


		Index_mutex.unlock();
		__sync_fetch_and_add(&writeIndexCount, 1);
	}

	pthread_exit(NULL);
}

//trade
void* saveTradeIntoFiles(void* arg) {
	int writeAccess = -1;
	while (1) {


		writeAccess = writeTradeCount % MAX_LEN;
		if (writeTradeCount >= TradeCount)
		{
			continue;
		}
		// if ((strncmp(g_Trade[writeAccess].securityId, "60", 2) * strncmp(g_Trade[writeAccess].securityId, "68", 2)) != 0)
		// {	
		// 	continue;
		// }
		Trade_mutex.lock();


		//输出到SH_Trade_File
		fprintf(SH_Trade_File, "%6.6s.SH,%ld,%ld,%lld,%lld,%lld,%d,%d,%lld,%lld\n",
			g_Trade[writeAccess].securityId,
			g_Trade[writeAccess].date,
			g_Trade[writeAccess].time,
			g_Trade[writeAccess].tradeIndex,
			g_Trade[writeAccess].tradeBuyNo,
			g_Trade[writeAccess].tradeSellNo,
			g_Trade[writeAccess].tradeType,
			g_Trade[writeAccess].tradeBsFlag,
			g_Trade[writeAccess].tradePx,
			g_Trade[writeAccess].tradeQty/1000);

		if (writeTradeCount % trade_fflush == 0) {
			fflush(SH_Trade_File);
		}

		Trade_mutex.unlock();
		__sync_fetch_and_add(&writeTradeCount, 1);
	}
	pthread_exit(NULL);
}


int main() {



	int checkcode = 0;
	LoadConfig();
	CreateBaseFolder();
	l_fast_template = LoadTemplate(l_template_path);
	std::string LogonRequest_str = SetLogonRequest(p_ProtocolEdition, p_SenderCompID, p_TragetCompID, p_EbcryptMethod, p_HeartBtInt, checkcode);
	int l_sockfd = SocketLogon(LogonRequest_str);
		
	//std::string StaticRequest_str = SetStaticRequest(p_ProtocolEdition, "VSS", "VDE",1,10,0,10000,checkcode);
	//std::cout << StaticRequest_str << std::endl;
	//int Static_flag = send(l_sockfd, StaticRequest_str.c_str(), strlen(StaticRequest_str.c_str()), 0);

	DataHandOut(l_sockfd, l_fast_template);
	return 0;
	
}
