 /*
 *  PCM Real Audio Streamer + Client
 *
 *  Copyright (C) 2005-2006 Nick Kokkos <nkokkos@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  Streamer part was highly influenced to a great extent by this great work of
 *  Nicholas J Humfrey's work: http://www.aelius.com/njh/pcm6cast/old/


// don't forget to compile with : ws2_32.lib winmm.lib COMCTL32.LIB 

#include	"stdafx.h"
#include	"resource.h"
#include	<windows.h>
#include	"Wav_Header.h"

#include	"Commdlg.h"		// <-- used for OPENFILE DIALOG procedures
#include	<commctrl.h>	// <-- for progress bar
#include	<iostream>
#include	<fstream.h>
#include	<time.h>  
#include	<winsock.h>  
#include	<mmsystem.h>	// for multimedia API

#define		MAXIMUM_PACKET_SIZE		1536
#define		MAXIMUM_FILE_PATH		1024

// #include	<mmreg.h>
// #include	<malloc.h>
// #include	<stdio.h>
// #include	<stdlib.h>  
// #include	<string.h> 

///////////////////////// RTP HEADER /////////////////////////////////////////////


typedef struct  _rtp_hrt_t{
	 
       unsigned int version:2;   // protocol version 
       unsigned int p:1;         // padding flag 
       unsigned int x:1;         // header extension flag 
       unsigned int cc:4;        // CSRC count 
       unsigned int m:1;         // marker bit 
       unsigned int pt:7;        // payload type 
       unsigned int seq:16;		 // sequence number
	   unsigned int ts:32;		 // time stamp 
	   unsigned long ssrc:32;	 //	synchronization source    
 //    unsigned long csrc[1];	 //	optional CSRC list
	   SYSTEMTIME	servers_time;// not defined in RTP, we use it to calculate the end-to-end
								 // delay in packets' generation and death
}rtp_hrt_t;
 

////////////////////// PACKET TO SEND OUT ///////////////////////

typedef struct _Packet{
 	unsigned char buffer[MAXIMUM_PACKET_SIZE];
    unsigned int  bufferlen;
}Packet;

/////////////////////// FILE TO STREAM /////////////////////////


typedef struct _File_To_Stream {

	unsigned int	numblocks;			//  how many packets/blocks are we sending out?
	unsigned int	remaining_bytes;	//  refers to the last packet to send
	unsigned int	all_packets;		//  refers to the whole number of packets to be transfered

	char			FilePath[MAXIMUM_FILE_PATH];
	bool			File_Loaded;
	unsigned int	file_size;			// file size minus wav_header ( 44 bytes )
	Wav_Header		wav_header;
} File_To_Stream;



static	Packet			*outpdata = new  Packet [sizeof(outpdata)];
static 	rtp_hrt_t		*header   =	new  rtp_hrt_t[sizeof(header) ];
char	buffer[MAXIMUM_PACKET_SIZE];	// buffer for reading from file;

//////////////////////////////////////////////////////////////////////
// main thread to send packet from file 
 DWORD WINAPI SendPacketsFromFile(LPVOID lParam);
////////////////////// StreamServerDialog  CLASS ///////////////////////


 class StreamServerDialog {

public:

		HWAVEOUT			hWaveOut;			// handle to playback device	
		HWND				DialogHandle;		// handle to Dialog window
		
		SOCKET				my_socket;			// socket to use 
		struct sockaddr_in	SockAddr;		

		HANDLE				hFile;				// handle to file to be streamed 
		
		File_To_Stream		my_File_To_Stream;	// struct to hold streaming info
		int					PAYLOAD_SIZE;


		HANDLE				hThread;			// thread to start 
												
		DWORD				dwThreadId;

		HWND				ProgressBar;

		unsigned int		bytes_sent;			// used for measuring throughput
		unsigned int		time_elapsed;	    // used for measuring throughput

		HANDLE				hMutexStopButton;
		bool				StopButton;

		int					DelayFactor;		// factor to divide delay by


		// for packet streaming
		float				delay;					// delay in msec before sending packet out
		unsigned int		packet_counter; 		// points to the number of packets send 
		int					max;					// max delay
		float				mean_delay;				// need to explain?
		int					delay_packets;			// how many packets sent with delay
		int					no_delay_packets;		// how many packets sent without waiting;

		fstream 			outfile;				// write data into a file
		fstream				outfile2;				// write data into file bit rate achieved;


////////////////////////// CLASS METHODS ///////////////////////////////////////////////////



// Return the difference between two times as microseconds 


long StreamServerDialog::diff_timeval(struct timeval * ep, struct timeval * sp)
{
        if (sp->tv_usec > ep->tv_usec) {
                ep->tv_usec += 1000000;
                --ep->tv_sec;
        }
        return ((ep->tv_sec - sp->tv_sec) * 1000000L) +
            (ep->tv_usec - sp->tv_usec);
} 


//////////////////////////////////////////////////////////////////////
// Add microseconds to a timeval 
void StreamServerDialog::add_usec2timeval( struct timeval * tv, long add )
{
	tv->tv_usec += add;

	// Don't let usec overflow 
	while (tv->tv_usec > 1000000) {
		tv->tv_sec++;
		tv->tv_usec -= 1000000;
	}
}


//////////////////////////////////////////////////////////////////////
int StreamServerDialog::gettimeofday(struct timeval* tp) {
    DWORD t;
    t = timeGetTime();
    tp->tv_sec = t / 1000;
    tp->tv_usec = t % 1000;
    // 0 indicates that the call succeeded. 
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////


bool StreamServerDialog::OnInit(HWND hWnd) 
{

	DialogHandle	=	hWnd;	 
	return true;

}


//////////////////////////////////////////////////////////////////////////////
void StreamServerDialog::OnGetFile(StreamServerDialog *pParam)
{

	StreamServerDialog* pDlg=(StreamServerDialog*) pParam;

	// code found here for the dialog box in MSDN 

	OPENFILENAME	ofn;				// common dialog box structure
	char			szFile[260];        // buffer for file name
	HANDLE			hf;					// file handle
	Wav_Header		wav_header;			// header values for the file read
	BOOL			bSuccess;
	int				Payload_Block;


	// Initialize OPENFILENAME
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);

	ofn.hwndOwner = pDlg->DialogHandle;

	ofn.lpstrFile = szFile;

	// Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
	// use the contents of szFile to initialize itself.
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "All\0*.*\0Wav\0*.Wav\0";		// filter only .wav files
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	Payload_Block = GetDlgItemInt(pDlg->DialogHandle,IDC_COMBO1,&bSuccess,FALSE);
	if (bSuccess) 
		pDlg->PAYLOAD_SIZE = Payload_Block;

	// Display the Open dialog box. 
	if (GetOpenFileName(&ofn)==TRUE) 	
	{
	if (hf = CreateFile(ofn.lpstrFile,
					GENERIC_READ, 
					0, NULL,
					OPEN_EXISTING,
					0, 
					NULL) )
		{

		char	str[16];
		HWND	hWndEdit;
		DWORD	dwBytes;

		unsigned int	my_FileSize;		// size of the whole file to be streamed.
		
		my_FileSize = GetFileSize(hf,NULL);

		// read the header of the wav file and get its attributes 
		ReadFile(hf,&wav_header,sizeof(wav_header),&dwBytes,NULL);

		strcpy(pDlg->my_File_To_Stream.FilePath,ofn.lpstrFile);

		memcpy(&pDlg->my_File_To_Stream.wav_header,&wav_header,sizeof(wav_header));
		
		pDlg->my_File_To_Stream.file_size = my_FileSize - sizeof(wav_header);
		

		// calculate the number of packets to be sent according to 
		// the payload size we specified at the beginning of the file.
		// remaining bytes should be the last packet to be sent whose
		// size is smaller than the payload size.
		if ((pDlg->my_File_To_Stream.file_size % pDlg->PAYLOAD_SIZE) !=0) 
		{
		pDlg->my_File_To_Stream.numblocks = pDlg->my_File_To_Stream.file_size / pDlg->PAYLOAD_SIZE;
		pDlg->my_File_To_Stream.remaining_bytes = pDlg->my_File_To_Stream.file_size - (pDlg->my_File_To_Stream.numblocks * pDlg->PAYLOAD_SIZE); // remainder is always in bytes
		} else if ( (my_FileSize % pDlg->PAYLOAD_SIZE) == 0) 
		 pDlg->my_File_To_Stream.numblocks = my_FileSize / pDlg->PAYLOAD_SIZE;
	
		if (pDlg->my_File_To_Stream.remaining_bytes==0)
			pDlg->my_File_To_Stream.all_packets = pDlg->my_File_To_Stream.numblocks;
		else 
			pDlg->my_File_To_Stream.all_packets = pDlg->my_File_To_Stream.numblocks + 1;

		pDlg->my_File_To_Stream.File_Loaded = true;	


		// show how many packets we are about to send ..
		sprintf(&str[0],"%d",pDlg->my_File_To_Stream.all_packets);		
		SetDlgItemText(pDlg->DialogHandle,ID_Packets,str);


		hWndEdit = GetDlgItem(pDlg->DialogHandle,ID_PayloadSize);
		sprintf(&str[0],"%d",pDlg->PAYLOAD_SIZE);	
		SendMessage(hWndEdit,EM_SETSEL,0,128000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);

		
		// update info on the file path on the dialog box 
		hWndEdit = GetDlgItem(pDlg->DialogHandle,ID_FILEPATH);	
		SendMessage(hWndEdit,EM_SETSEL,0,256000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)pDlg->my_File_To_Stream.FilePath);


		// keep updating on the file attributes ..
		// FormatTag
		hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_FormatTag);
		sprintf(&str[0],"%d",pDlg->my_File_To_Stream.wav_header.wFormatTag);	
		SendMessage(hWndEdit,EM_SETSEL,0,256000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);
	
		// Channels
		hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_Channels);
		sprintf(&str[0],"%d",wav_header.nChannels);	
		SendMessage(hWndEdit,EM_SETSEL,0,256000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);

		// SamplesPerSecond
		hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_SamplesPerSec);
		sprintf(&str[0],"%d",wav_header.nSamplesPerSec);	
		SendMessage(hWndEdit,EM_SETSEL,0,256000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);

		// AvgBytesPerSec
		hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_AvgBytesPerSec);
		sprintf(&str[0],"%d",wav_header.nAvgBytesPerSec);	
		SendMessage(hWndEdit,EM_SETSEL,0,256000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);

		// BitsPerSample
		hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_BitsPerSample);
		sprintf(&str[0],"%d",wav_header.nBitsPerSample);	
		SendMessage(hWndEdit,EM_SETSEL,0,256000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);

		// BitsPerSample
		hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_BlockAlign);
		sprintf(&str[0],"%d",wav_header.nBlockAlign);	
		SendMessage(hWndEdit,EM_SETSEL,0,128000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);	

		// FILE SIZE
		hWndEdit = GetDlgItem(pDlg->DialogHandle,ID_FILESIZE);
		sprintf(&str[0],"%d",my_FileSize);	
		SendMessage(hWndEdit,EM_SETSEL,0,128000);
		SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)str);

		
		CloseHandle(hf); // close file .. we got the info we wanted.


		} // if (hf = CreateFile(ofn.lpstrFile, .....
	

  
	} // 	if (GetOpenFileName(&ofn)==TRUE) ....


} 

///////////////////////////////////////////////////////////////////////////////////////

void StreamServerDialog::StreamFile( StreamServerDialog *pDialog)
{

		// start the thread to send 
		// create mutexes for handling the "stop" thread button		
		StreamServerDialog* pDlg = (StreamServerDialog*) pDialog;
	
		pDlg->hMutexStopButton = CreateMutex(NULL,0,NULL);
	
		WaitForSingleObject(pDlg->hMutexStopButton,INFINITE);
		pDlg->StopButton = false;
		ReleaseMutex(pDlg->hMutexStopButton);


		//	pDlg->outfile.open("output.txt", ios::out | ios::app);

		/*
		pDlg->outfile.open("server_data.txt", ios::out);
		pDlg->outfile << "time_elapsed" << " ";
		pDlg->outfile << "Instantaneous_Delay"   << " ";
		pDlg->outfile << "Instantaneous_Delay/Divisor Factor" << " ";
		pDlg->outfile << "Delay_Packets" << " ";
		pDlg->outfile << "No_Delay_Packets" << " \n";


		pDlg->outfile2.open("server_data2.txt",ios::out);
		*/
		
		/*

		pDlg->outfile << "time_elapsed" << " ";
		pDlg->outfile << "bytes_sent"   << " ";
		pDlg->outfile << "Average_BytesPerSecond" << " ";
		pDlg->outfile << "packet_counter" << " ";
		pDlg->outfile << "no_delay_packets" << " "; 
		pDlg->outfile << "delay_packets" << " "; 
		pDlg->outfile << "mean_delay" << " ";
		pDlg->outfile << "max_delay"<< " ";
		pDlg->outfile << "Current_Delay" << " ";
		pDlg->outfile << "DelayFactor" << "\n";

*/

//		pDlg->outfile << bytes

		pDlg->hThread = CreateThread(
        NULL,							// default security attributes 
        0,								// use default stack size  
		SendPacketsFromFile,			// thread function 
        pDlg,							// pass pDlg so we can have access to the class 
        0,								// use default creation flags 
        &pDlg->dwThreadId);	
}



// function to show the bytes sent every 1 sec 
void StreamServerDialog::UpdateThroughput(StreamServerDialog *pDialog)
{

//		HWND	hWndEdit;
		char str[16];
		char str2[16];

		StreamServerDialog* pDlg = (StreamServerDialog*) pDialog;

		pDlg->time_elapsed = pDlg->time_elapsed + 1;

	
		sprintf(&str[0],"%u",pDlg->bytes_sent / pDlg->time_elapsed );	// get the throughput			
		SetDlgItemText(pDlg->DialogHandle,ID_Timer,str);

		sprintf(&str2[0],"%u",pDlg->time_elapsed );		
		SetDlgItemText(pDlg->DialogHandle,ID_SecondsElapsed,str2);

	/*	
	
		if ( !pDlg->outfile2.is_open() ) {
		// The file could not be opened
		}
		else 
		
		{
			pDlg->outfile2 << str    << " ";
			pDlg->outfile2 << str2   << " ";
			pDlg->outfile2 << "\n";
		}

	






		// that's for writting data out to file..



		if ( !pDlg->outfile.is_open() ) {
		// The file could not be opened
		}
		else 
		
		{
				
		pDlg->outfile << pDlg->time_elapsed << " ";
		pDlg->outfile << pDlg->bytes_sent   << " ";
		pDlg->outfile << (pDlg->bytes_sent / pDlg->time_elapsed) << " ";
		pDlg->outfile << pDlg->packet_counter << " ";
		pDlg->outfile << pDlg->no_delay_packets << " "; 
		pDlg->outfile << pDlg->delay_packets << " "; // " " << "\n";
		pDlg->outfile << pDlg->mean_delay << " ";
		pDlg->outfile << pDlg->max << " ";
		if (pDlg->delay < 0) 
			pDlg->outfile << ( 0 / pDlg->DelayFactor )  << " ";
		else
			pDlg->outfile << ( pDlg->delay / pDlg->DelayFactor )  << " "; 
	

		pDlg->outfile << pDlg->DelayFactor << "\n";

		}
		
		return;

	*/



	}


};  // END CLASS METHOD LISTINGS
 

//////////////////// SendPacketsFromFile THREAD  ( GLOBAL FUNCTION ) /////////////////

DWORD WINAPI SendPacketsFromFile(LPVOID lParam)
{
	
	StreamServerDialog* pDlg = (StreamServerDialog*) lParam;

	bool			LocalStopButton = false;

	char			IPADDRESS[128];
	int				PORT,iPort,iDelayFactor;
	BOOL			int_success;

	GetDlgItemText(pDlg->DialogHandle,ID_IPAddress, IPADDRESS,sizeof(IPADDRESS) );
//	Dotted_Decimal_IP_Address = inet_addr(ID_IPAddress);

	iPort = GetDlgItemInt(pDlg->DialogHandle,ID_Port,&int_success,FALSE);
	if (int_success)
		PORT = iPort;

	iDelayFactor = GetDlgItemInt(pDlg->DialogHandle,IDC_COMBO2,&int_success,FALSE);
	if (int_success)
		pDlg->DelayFactor = iDelayFactor;


	int packet_len = pDlg->PAYLOAD_SIZE + sizeof(rtp_hrt_t);
	int seq_num; 
	int time_stamp;


	unsigned int	myTimerEvent=0;
	bool			mySetTimer = false;


	DWORD		readBytes = 0;
	Wav_Header	wav_header;
	struct		timeval due, now;
	int			payload_frames = pDlg->PAYLOAD_SIZE / pDlg->my_File_To_Stream.wav_header.nBlockAlign;
	int			payload_duration = payload_frames * ((float)1/pDlg->my_File_To_Stream.wav_header.nSamplesPerSec * 1000000);


	int		delay_counter		= 0;
	float	delay_sum			= 0;

	pDlg->bytes_sent		= 0;
	pDlg->time_elapsed		= 0;
	pDlg->delay				= 0;
	pDlg->packet_counter	= 0;			// points to the number of packets send 
	pDlg->max				= 0;
	pDlg->mean_delay		= 0;
	pDlg->delay_packets		= 0;
	pDlg->no_delay_packets  = 0;
	


	// used for the progress bar.. 
	InitCommonControls(); 	
	pDlg->ProgressBar = GetDlgItem(pDlg->DialogHandle,IDC_PROGRESS1);
	SendMessage(pDlg->ProgressBar, PBM_SETRANGE, 0, MAKELPARAM(1, pDlg->my_File_To_Stream.all_packets));

//	pDlg->outfile.open("output.txt", ios::out | ios::app);

/*

	pDlg->outfile.open("server_data.txt", ios::out);
	pDlg->outfile << "time_elapsed" << " ";
	pDlg->outfile << "bytes_sent"   << " ";
	pDlg->outfile << "Average Bytes/Second" << " ";
	pDlg->outfile << "packet_counter" << " ";
	pDlg->outfile << "no_delay_packets" << " "; 
	pDlg->outfile << "delay_packets" << " "; 
	pDlg->outfile << "mean_delay" << " ";
	pDlg->outfile << "max_delay"<< " ";
	pDlg->outfile << "Current_Delay" << " ";
	pDlg->outfile << "DelayFactor" << "\n";
*/


	char str[16];

	HANDLE	hFile;

	// The standard RTP header	stack. I don't use the optional fields 
	header->version=2;
	header->p=0;
	header->x=0;
	header->cc=0;
	header->m=1;
	header->pt = 10; // <----
	// payload type = 10 look in RFC 3550 and RFC 3551
	// RTP Profile for Audio and Video Conferences with Minimal Control:		
	// payload Type   encoding name  media type       clock rate   channels                                        	
	//	10				L16               A           44,100          2


	// Initialise the sequence number and timestamp & 
	// encode to network byte order// 
	header->seq =  htons(rand());		
	header->seq =  header->seq & 65535; 
	header->ts  =  htonl(rand());


	 if((hFile = CreateFile(pDlg->my_File_To_Stream.FilePath,
		 GENERIC_READ,
		 FILE_SHARE_READ,
		 NULL,
		 OPEN_EXISTING,
		 0,
		 NULL))
     == INVALID_HANDLE_VALUE) 
     {
        MessageBox(NULL,"unable to open wav file","File Error",NULL);
     //   ExitProcess(1);
		CloseHandle(pDlg->hThread);

    }

	// read the wav header so move forward by 44 bytes(that is, 44 bytes = wav file's header size)
	ReadFile(hFile,&wav_header,sizeof(wav_header),&readBytes,NULL);

	// create the socket for streaming ..  
	if ((pDlg->my_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) 
	{

		MessageBox(NULL,"socket creation error","Socket Error",NULL);
		KillTimer(pDlg->DialogHandle,myTimerEvent);
	//	ExitProcess(1);
	}

	// create the address and port. show stuff about packets..
	memset(&pDlg->SockAddr,0,sizeof(pDlg->SockAddr));
	pDlg->SockAddr.sin_family		=	AF_INET;
	pDlg->SockAddr.sin_port			=	htons(PORT);
	pDlg->SockAddr.sin_addr.s_addr	=	inet_addr(IPADDRESS);

	sprintf(&str[0],"%d",payload_duration);		
	SetDlgItemText(pDlg->DialogHandle,ID_PayloadDuration,str);
	
	sprintf(&str[0],"%d",payload_frames);		
	SetDlgItemText(pDlg->DialogHandle,ID_FramesPerPacket,str);

	sprintf(&str[0],"%d",pDlg->PAYLOAD_SIZE);	
	SetDlgItemText(pDlg->DialogHandle,ID_PayloadSize,str);


	//  Get the time now...
	
	pDlg->gettimeofday(&due);


	//  START SENDING ... main loop 

	// get out of loop if we press "Stop Thread" or if we reach the end of file..	
	while (!LocalStopButton && pDlg->packet_counter <= pDlg->my_File_To_Stream.all_packets)
	{

		seq_num		=  header->seq;
		time_stamp  =  header->ts;

	//  Encode to network byte order 
	//	header->seq = htons(seq_num);
	//	header->ts  = htonl(time_stamp);

		pDlg->add_usec2timeval( &due, payload_duration );	

		if(!ReadFile(hFile, buffer, pDlg->PAYLOAD_SIZE, &readBytes, NULL))
            break;
        if(readBytes == 0) {
            break; // reached the end of file ? 
		}

		if(readBytes < sizeof(buffer) )			
           memset(buffer + readBytes, 0, sizeof(buffer) - readBytes);

		// Copy read data + header values to outpdata buffer
		// Insert into the header to time the packet was created.
		GetSystemTime(&header->servers_time);
		memcpy(outpdata->buffer,header,sizeof(rtp_hrt_t));		
		memcpy(outpdata->buffer + sizeof(rtp_hrt_t),buffer,readBytes);
		outpdata->bufferlen = readBytes + sizeof(rtp_hrt_t);
			
		// How long do we wait to get the data?
                pDlg->gettimeofday(&now);
		pDlg->delay = pDlg->diff_timeval(&due, &now);	
		pDlg->delay = pDlg->delay / 1000;					// convert to milliseconds 

		float fnow,fdue;

		fnow = now.tv_sec + ((float)now.tv_usec / 1000000);
		fdue = due.tv_sec + ((float)due.tv_usec / 1000000);


		sprintf(&str[0],"%f",fnow);		
		SetDlgItemText(pDlg->DialogHandle,ID_Now,str);
	
		sprintf(&str[0],"%f",fdue);		
		SetDlgItemText(pDlg->DialogHandle,ID_Due,str);

		sprintf(&str[0],"%f",fdue-fnow);		
		SetDlgItemText(pDlg->DialogHandle,ID_Diff,str);

		if (pDlg->delay < 0)          // negative delay means no delay!!!! 
		{							  // send the packet out now!!		
		   pDlg->delay = 0;
		   pDlg->no_delay_packets++;
		} 		
		else if 		
		(pDlg->delay > 0) 		
		{   
			delay_counter++;
			Sleep( pDlg->delay / pDlg->DelayFactor  ); //wait pDlg->delay / Delay Factor
			pDlg->delay_packets++;						

			if ((pDlg->delay/pDlg->DelayFactor) > pDlg->max ) 
				pDlg->max = pDlg->delay/pDlg->DelayFactor ;
			else
				pDlg->max = pDlg->max;
			
			if (delay_counter == 0)		// silly code. Its purpose here's to prevent
				delay_counter++;	    // devision by zero. Should 
										// run only once 
			
			delay_sum = delay_sum + (float)(pDlg->delay/pDlg->DelayFactor) ; // used here to calculate
			pDlg->mean_delay = (float)(delay_sum / delay_counter);			// the mean delay
		} 

		pDlg->gettimeofday(&now);	// get the time now and store to file..
		float fnow2;
		fnow2 = now.tv_sec + ((float)now.tv_usec / 1000000);	

		sprintf(&str[0],"%f",fnow2);		

//		pDlg->outfile << str << " " << pDlg->delay << " " << (pDlg->delay/pDlg->DelayFactor) <<
//			" " << pDlg->delay_packets << " "  << pDlg->no_delay_packets << "\n";

	//  Time to send the packet 
	if ( sendto(pDlg->my_socket,
		(const char*)&outpdata->buffer,
		outpdata->bufferlen,0, (struct sockaddr *)&(pDlg->SockAddr),
		sizeof(pDlg->SockAddr) ) !=  (int)outpdata->bufferlen) 
		{
			MessageBox(NULL," could not send packets","File Streamer",NULL);
			CloseHandle(pDlg->hThread);
			KillTimer(pDlg->DialogHandle,myTimerEvent);
		//	ExitProcess(1);	
		}
	
		// when we start sending for the first time we set the timer to stuff on the dialog
		// later on we do not need to reset it
	if (mySetTimer == false) 
		{
			SetTimer(pDlg->DialogHandle,myTimerEvent,1000,NULL);// set interrupt every 1000 msec 
			mySetTimer = true;
		}	
			
		// get the real streamed data output ( only payload) 
		pDlg->bytes_sent = pDlg->bytes_sent + outpdata->bufferlen - sizeof(rtp_hrt_t);
		pDlg->packet_counter++; // increment the counter . read next block of data from file

		// Update RTP header fields
		// increase timestamp by order of payload size / frame size
		// Frame size for 44.1 khz, 2 channel, 16 bit audio is 2 * channels
		header->ts = time_stamp + packet_len / (2* pDlg->my_File_To_Stream.wav_header.nChannels);
		header->seq = seq_num + 1;
		
		// update the packets send 
		sprintf(&str[0],"%d",pDlg->packet_counter);	
		SetDlgItemText(pDlg->DialogHandle,ID_PacketsSent,str);	

		
//		GetSystemTime(&pDlg->);
//		sprintf(stimeresult, "%02i-%02i-%0004i %02i:%02i:%02i:%03i\0", stime.wDay, stime.wMonth, stime.wYear, stime.wHour, stime.wMinute, stime.wSecond,stime.wMilliseconds);
//		SetDlgItemText(pDlg->DialogHandle,IDC_CurrentTime,stimeresult);

		// update the packets send 
		sprintf(&str[0],"%d",outpdata->bufferlen);	
		SetDlgItemText(pDlg->DialogHandle,ID_PacketLen,str);

		// update the max delay
		sprintf(&str[0],"%d",pDlg->max);	
		SetDlgItemText(pDlg->DialogHandle,ID_MaxDelay,str);	

		// update the mean delay
		sprintf(&str[0],"%f",pDlg->mean_delay);	
		SetDlgItemText(pDlg->DialogHandle,ID_MeanDelay,str);	

		// update the delay packets
		sprintf(&str[0],"%d",pDlg->delay_packets);	
		SetDlgItemText(pDlg->DialogHandle,ID_DelayPackets,str);
		
		// update the no_delay packets
		sprintf(&str[0],"%d",pDlg->no_delay_packets);	
		SetDlgItemText(pDlg->DialogHandle,ID_no_delay_packets,str);

		sprintf(&str[0],"%f",pDlg->delay/pDlg->DelayFactor);		
		SetDlgItemText(pDlg->DialogHandle,ID_Delay,str);
	

		/////////////// update dialog for RTP Header values ///////
		
		sprintf(&str[0],"%d",header->version );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_Version,str);	

		sprintf(&str[0],"%d",header->p );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_p,str);	

		 sprintf(&str[0],"%d",header->x );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_x,str);

		sprintf(&str[0],"%d",header->cc );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_cc,str);

		sprintf(&str[0],"%d",header->m );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_m,str);

		sprintf(&str[0],"%d",header->pt );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_pt,str);

		sprintf(&str[0],"%u",header->seq );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_seq,str);

		sprintf(&str[0],"%u",header->ts );		
		SetDlgItemText(pDlg->DialogHandle,ID_Rtp_ts,str);

		// handle the click on "Stop Thread" Button			
		WaitForSingleObject(pDlg->hMutexStopButton,INFINITE);
		LocalStopButton = pDlg->StopButton;
		ReleaseMutex(pDlg->hMutexStopButton);

		SendMessage(pDlg->ProgressBar,(UINT) PBM_SETPOS, (WPARAM) pDlg->packet_counter, 0);


		} // while (!LocalStopButton && packet_counter < pDlg->my_File_To_Stream.all_packets)


		if ( pDlg->outfile.is_open() )
			pDlg->outfile.close();


		KillTimer(pDlg->DialogHandle,myTimerEvent);
		CloseHandle(hFile);
		closesocket(pDlg->my_socket);
		CloseHandle(pDlg->hThread);

		
		return 0;	
	
}


//////////////////////////// GLOBAL FUNCTIONS /////////////////////////////////////////////


int CALLBACK DialogProc(HWND hDlg, UINT MSG, WPARAM wParam, LPARAM lParam)
{

	// STEP # 2	

	StreamServerDialog	*pDialog;

	pDialog = (StreamServerDialog *) GetWindowLong(hDlg,DWL_USER);

	const char *PayloadValues[] = { "128", "256", "384",
                                "512", "640", "768",
                                "896", "1024", "1280",
                                "1401" };

	const char *DelayFactor[] = {"1","2","3","4","5"};




	// for progress bar control
	INITCOMMONCONTROLSEX InitCtrlEx;
	InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
	InitCtrlEx.dwICC  = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&InitCtrlEx);

	switch(MSG)
	{
	
		case WM_TIMER: 

			pDialog->UpdateThroughput((StreamServerDialog *) pDialog);
		break;  

		
		case WM_COMMAND:

					switch(LOWORD(wParam)) 
					{
					
						case ID_CANCEL:

							EndDialog(pDialog->DialogHandle, (INT_PTR) 0);   // quit
						break;
				
							case ID_STOP:
							WaitForSingleObject(pDialog->hMutexStopButton,INFINITE);
							pDialog->StopButton = true;
							ReleaseMutex(pDialog->hMutexStopButton);
							break;

						case ID_GETFILE:
							pDialog->OnGetFile((StreamServerDialog *) pDialog);
						break;
						case ID_Stream_File:
							pDialog->StreamFile((StreamServerDialog *) pDialog);
						break;

						}
		break;

			
		case WM_INITDIALOG:			
			
			HWND	item;
			int		i;

			// fill the pop up menu
			item = GetDlgItem(hDlg,IDC_COMBO1);

			for (i = 0; i < 10; i++)  
			{
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)PayloadValues[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 7, 0);


			item = GetDlgItem(hDlg,IDC_COMBO2);

			for (i = 0; i < 5; i++)  
			{
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)DelayFactor[i]));
			}
			
			
			SendMessage(item, CB_SETCURSEL, 1, 0);
			


			SetWindowLong(hDlg,DWL_USER,lParam);
			pDialog =  (StreamServerDialog*) lParam;					
			return pDialog->OnInit(hDlg); // true 
			
	} //switch(MSG)

	return false;

}


// main program 
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
 
	// STEP #1 


	WSADATA					ws_data;				// microsoft stuff

	WORD					w_Version_Requested;	// microsoft stuff
		
	StreamServerDialog		myStreamServerDialog;

	w_Version_Requested =	MAKEWORD(1,1);			// microsoft stuff

	WSAStartup(w_Version_Requested,&ws_data);
 
	DialogBoxParam(hInstance,MAKEINTRESOURCE(IDD_DIALOG1),NULL,DialogProc,
		(long)&myStreamServerDialog);

	WSACleanup();									// microsoft stuff

	return 0;

}
