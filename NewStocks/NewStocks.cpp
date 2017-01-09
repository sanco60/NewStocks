#include "stdafx.h"
#include "plugin.h"
#include "stdio.h"

#include "MemLeaker.h"
#include <string>
#include <sstream>
#include <fstream>
#include <io.h>
#include <iomanip>


BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}

PDATAIOFUNC	 g_pFuncCallBack;

//获取回调函数
void RegisterDataInterface(PDATAIOFUNC pfn)
{
	g_pFuncCallBack = pfn;
}

//注册插件信息
void GetCopyRightInfo(LPPLUGIN info)
{
	//填写基本信息
	strcpy_s(info->Name, "新股信息提取");
	strcpy_s(info->Dy, "US");
	strcpy_s(info->Author, "john");
	strcpy_s(info->Period, "短线");
	strcpy_s(info->Descript, "新股信息提取");
	strcpy_s(info->OtherInfo, "新股信息提取");
	//填写参数信息
	info->ParamNum = 0;
	
	/*strcpy_s(info->ParamInfo[0].acParaName, "百分比");
	info->ParamInfo[0].nMin=0;
	info->ParamInfo[0].nMax=1000;
	info->ParamInfo[0].nDefault=20;*/

}

////////////////////////////////////////////////////////////////////////////////
//自定义实现细节函数(可根据选股需要添加)

const	BYTE	g_nAvoidMask[]={0xF8,0xF8,0xF8,0xF8};	// 无效数据标志(系统定义)

//char* g_nFatherCode[] = { "999999", "399001", "399005", "399006" };

const int cIgnoreStocksMaxCount = 5000;
char g_IgnoreStocks[cIgnoreStocksMaxCount][7];

bool g_bInitial = false;

const char g_UserDir[] = {".\\UserData\\"};

const char g_IgnoreKeyword[] = {"IGS_NewStocks*.EBK"};


typedef struct tag_STOCKKEYS
{
	char Name[9];
	NTime startDate;
	NTime endDate;
	float publishPrice;
	float topPrice;
} STOCKKEYS;


LPHISDAT maxClose(LPHISDAT pHisDat, long lDataNum)
{
	if (NULL == pHisDat || lDataNum <= 0)
		return NULL;

	LPHISDAT pMax = pHisDat;
	for (long i = 0; i < lDataNum; i++)
	{
		if (pMax->Close < (pHisDat+i)->Close)
			pMax = pHisDat+i;
	}
	return pMax;
}


BOOL fEqual(double a, double b)
{
	const double fJudge = 0.01;
	double fValue = 0.0;

	if (a > b)
		fValue = a - b;
	else 
		fValue = b - a;

	if (fValue > fJudge)
		return FALSE;

	return TRUE;
}


BOOL dateEqual(NTime t1, NTime t2)
{
	if (t1.year != t2.year || t1.month != t2.month || t1.day != t2.day)
		return FALSE;

	return TRUE;
}


int dateComp(NTime& nLeft, NTime& nRight)
{
	if (nLeft.year < nRight.year)
		return -1;
	else if (nLeft.year > nRight.year)
		return 1;

	if (nLeft.month < nRight.month)
		return -1;
	else if (nLeft.month > nRight.month)
		return 1;

	if (nLeft.day < nRight.day)
		return -1;
	else if (nLeft.day > nRight.day)
		return 1;

	return 0;
}


#define DATE_LEFT_EARLY(L, R) (-1 == dateComp(L, R))

#define DATE_LEFT_LATER(L, R) (1 == dateComp(L, R))


/* 过滤函数
   返回值：以S和*开头的股票 或者 上市不满一年，返回FALSE，否则返回TRUE
*/
BOOL filterStock(char * Code, short nSetCode, NTime time1, NTime time2, BYTE nTQ)
{
	if (NULL == Code)
		return FALSE;

	//{
	//	//用户指定的股票需要屏蔽
	//	for (int iRow = 0; iRow < cIgnoreStocksMaxCount; iRow++)
	//	{
	//		if (0 == strlen(g_IgnoreStocks[iRow]))
	//			break;

	//		if (0 == strcmp(g_IgnoreStocks[iRow], Code))
	//		{
	//			OutputDebugStringA("User abandon ");
	//			return FALSE;
	//		}
	//	}
	//}

	const short cInfoNum = 2;
	short iInfoNum = cInfoNum;

	{
		STOCKINFO stockInfoArray[cInfoNum];
		memset(stockInfoArray, 0, cInfoNum*sizeof(STOCKINFO));

		LPSTOCKINFO pStockInfo = stockInfoArray;

		//获取上市时间
		long readnum = g_pFuncCallBack(Code, nSetCode, STKINFO_DAT, pStockInfo, iInfoNum, time1, time2, nTQ, 0);
		if (readnum < 1)
		{
			OutputDebugStringA("g_pFuncCallBack get start date error.");
			pStockInfo = NULL;
			return FALSE;
		}

		NTime startDate;
		memset(&startDate, 0, sizeof(startDate));

		long lStartDate = pStockInfo->J_start;
		startDate.year = (short)(lStartDate / 10000);
		lStartDate = lStartDate % 10000;
		startDate.month = (unsigned char)(lStartDate / 100);
		lStartDate = lStartDate % 100;
		startDate.day = (unsigned char)lStartDate;
		
		if (DATE_LEFT_EARLY(startDate, time1) || DATE_LEFT_LATER(startDate, time2))
		{
			pStockInfo = NULL;
			return FALSE;
		}
	}

	return TRUE;
}


/* 提取新股信息 */
bool pickUpInfo(char * Code, short nSetCode, short DataType, NTime time1, NTime time2, BYTE nTQ, STOCKKEYS& stKey)
{
	//窥视数据个数
	long datanum = g_pFuncCallBack(Code, nSetCode, DataType, NULL, -1, time1, time2, nTQ, 0);
	if ( 1 >= datanum )
	{
		std::ostringstream ss;
		ss << "datanum = " << datanum << " ";
		OutputDebugStringA(ss.str().c_str());
		return false;
	}

	LPHISDAT pHisDat = new HISDAT[datanum];
	memset(pHisDat, 0, datanum*sizeof(HISDAT));

	long readnum = g_pFuncCallBack(Code, nSetCode, DataType, pHisDat, (short)datanum, time1, time2, nTQ, 0);
	if ( 1 >= readnum || readnum > datanum )
	{
		std::ostringstream ss;
		ss << "readnum = " << readnum << " datanum = " << datanum << " ";
		OutputDebugStringA(ss.str().c_str());
		delete[] pHisDat;
		pHisDat = NULL;
		return false;
	}

	float fYClose = pHisDat->Close;

	LPHISDAT pIndex = pHisDat + 1;
	LPHISDAT pEndHisDat = pHisDat + readnum;

	for (; pIndex < pEndHisDat; pIndex++)
	{
		if (!fEqual(pIndex->Close, fYClose*1.1))
		{
			break;
		}
		fYClose = pIndex->Close;
	}

	//没有打开涨停
	//if (pIndex == pEndHisDat)
	//{
	//	delete[] pHisDat;
	//	pHisDat=NULL;
	//	return false;
	//}

	STOCKINFO stockInfoArray[1];
	memset(stockInfoArray, 0, sizeof(STOCKINFO));
	g_pFuncCallBack(Code, nSetCode, STKINFO_DAT, stockInfoArray, 1, time1, time2, nTQ, 0);
	
	memcpy(stKey.Name, stockInfoArray[0].Name, 9);
	stKey.startDate = pHisDat->Time;
	stKey.publishPrice = pHisDat->Close / (float)1.44;
	stKey.topPrice = fYClose;
	stKey.endDate = pIndex->Time;
		
	delete[] pHisDat;
	pHisDat=NULL;

	return true;
}


void restoreIgnoreStocks()
{
	long lFind = 0;
	struct _finddata_t fInfo;
	std::string szFind = g_UserDir;
	szFind.append(g_IgnoreKeyword);

	memset(g_IgnoreStocks, 0, sizeof(g_IgnoreStocks));

	lFind = _findfirst(szFind.c_str(), &fInfo);
	if (-1 == lFind)
		return;

	int iRow = 0;
	do{
		std::string szFilePath = g_UserDir;
		szFilePath.append(fInfo.name);
		
		std::ifstream ifs;
		ifs.open(szFilePath);
		if (!ifs.is_open())
			continue;

		while(!ifs.eof())
		{
			char szLine[64] = {0};
			ifs.getline(szLine, 63);
			std::string dataLine(szLine+1);
			if (6 == dataLine.length())
			{
				if (iRow >= cIgnoreStocksMaxCount)
					break;
				strcpy_s(g_IgnoreStocks[iRow++], dataLine.c_str());
			}
		}
		ifs.close();
		
	} while (0 == _findnext(lFind, &fInfo));
	_findclose(lFind);

	return;
}



bool init()
{
	if (g_bInitial)
		return false;

	restoreIgnoreStocks();

	std::string szOutPath;
	std::ofstream ofs;

	szOutPath = g_UserDir;
	szOutPath += "NewStocksOutput.txt";
		
	ofs.open(szOutPath);
	ofs.close();

	g_bInitial = true;

	return true;
}




BOOL InputInfoThenCalc1(char * Code,short nSetCode,int Value[4],short DataType,short nDataNum,BYTE nTQ,unsigned long unused) //按最近数据计算
{
	BOOL nRet = FALSE;
	return nRet;
}

BOOL InputInfoThenCalc2(char * Code,short nSetCode,int Value[4],short DataType,NTime time1,NTime time2,BYTE nTQ,unsigned long unused)  //选取区段
{
	BOOL nRet = FALSE;

	{
	std::ofstream ofs;
	std::string szOutPath;
	std::ostringstream oss;

	if ( (Value[0] < 0 || Value[0] > 1000) 
		|| (Value[1] != 0 && Value[1] != 1)  
		|| NULL == Code )
	{
		OutputDebugStringA("Parameters Error!\n");
		goto endCalc2;
	}		

	if (!g_bInitial)
		init();

	/* 除了该时间段内发行的新股 其它股票均过滤掉 */
	if (FALSE == filterStock(Code, nSetCode, time1, time2, nTQ))
	{
		OutputDebugStringA("===== filter stock : ");
		OutputDebugStringA(Code);
		OutputDebugStringA(" =========\n");
		goto endCalc2;
	}

	/* 提取新股信息 */
	STOCKKEYS stKeys;
	memset(&stKeys, 0, sizeof(stKeys));
	if (!pickUpInfo(Code, nSetCode, DataType, time1, time2, nTQ, stKeys))
	{
		OutputDebugStringA(Code);
		OutputDebugStringA("=========== son calcUpPercent error!!\n");
		goto endCalc2;
	}

	oss << std::fixed << std::setprecision(2);	
	oss << Code << "\t";
	oss << stKeys.Name << "\t";
	oss << stKeys.publishPrice << "\t";
	oss << stKeys.topPrice << "\t";
	oss << (UINT)stKeys.startDate.year << "-" << (UINT)stKeys.startDate.month << "-" << (UINT)stKeys.startDate.day << "\t";
	oss << (UINT)stKeys.endDate.year << "-" << (UINT)stKeys.endDate.month << "-" << (UINT)stKeys.endDate.day << "\n";

	szOutPath = g_UserDir;
	szOutPath += "NewStocksOutput.txt";
		
	ofs.open(szOutPath, std::ios::app);
	ofs << oss.str();
	ofs.close();

	nRet = TRUE;

	}
endCalc2:
	MEMLEAK_OUTPUT();

	return nRet;
}
