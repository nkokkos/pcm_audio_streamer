// test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <windows.h>
#include <iostream.h>
#include <fstream.h>
#include <time.h>

typedef struct  _rtp_hrt_t{
	 
       unsigned int version:2;   // protocol version 
       unsigned int p:1;         // padding flag 
       unsigned int x:1;         // header extension flag 
       unsigned int cc:4;        // CSRC count 
       unsigned int m:1;         // marker bit 
       unsigned int pt:7;        // payload type 
       unsigned int seq:16;		 // sequence number
	   unsigned int ts:32;		 // time stamp
	   unsigned long ssrc:32;    // source identification 
 //	   optional CSRC list 
 //    unsigned long csrc[1];
	   SYSTEMTIME	time_stamp;
} rtp_hrt_t;


class Time
{
private:
	short hours;
	short minutes;
	short seconds;
	short milliseconds;
public:
	Time(short h, short m, short s,short mi);
	void NormalizeTime(void);
	void Display(void);
	Time *operator+(Time aTime);
	void operator*=(short num);
};

Time::Time(short h, short m, short s,short mi)
{
	hours = h;
	minutes = m;
	seconds = s;
	milliseconds = mi;

	NormalizeTime();
}

void Time::NormalizeTime(void)
{ 
	hours   = hours +  ((minutes + (seconds/60)) / 60);
	minutes =    ((minutes + (seconds/60)) % 60);
	// was : seconds =  seconds  = seconds % 60
	seconds = ((seconds + (milliseconds / 1000)) % 60);
	milliseconds = milliseconds % 1000;


}

void Time::Display(void)
{
	cout << "(" << hours << ":" << minutes << ":" << seconds << ":" << milliseconds << ")\n";
}

Time *Time::operator+(Time aTime)
{
	short h;
	short m;
	short s;
	short mil;
	Time *tempTimePtr;

	h = hours + aTime.hours;
	m = minutes + aTime.minutes;
	s = seconds + aTime.seconds;
	mil = milliseconds + aTime.milliseconds;

	tempTimePtr = new Time(h,m,s,mil);

	return tempTimePtr;
}



void Time::operator *=(short num)
{
	hours  *= num;
	minutes *= num;
	seconds *= num;
	milliseconds *= num;

	NormalizeTime();
}



int main(int argc, char* argv[])
{

	
	SYSTEMTIME	my_time;

	char _mytime[100];

	GetSystemTime(&my_time);

	printf("sizeof rtp packet: %d\n\n\n", sizeof(rtp_hrt_t));

	sprintf(_mytime,"%02i:%02i:%02i:%03i\0",my_time.wHour,my_time.wMinute,
		my_time.wSecond,my_time.wMilliseconds);
	
	printf("my_time.wDay is: %s\n\n",_mytime);
	printf("Hello World!\n");


	Time firstTime(1,58,56,800);
	Time secondTime(2,4,6,350);
	
	Time *sumTimePtr;

	
	firstTime.Display();
	secondTime.Display();

	cout << "-----------\n";

	sumTimePtr = firstTime + secondTime;

	sumTimePtr->Display();

	cout << "*       3 \n";
	cout << "------------\n";

	(*sumTimePtr) *= 3;
	sumTimePtr->Display();
	
	return 0;
}



