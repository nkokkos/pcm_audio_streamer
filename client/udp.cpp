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

 *  Client part was based on David Overton's amazing article about playing audio on 
 *  windows using waveout function calls : 
 *  http://www.planet-source-code.com/vb/scripts/ShowCode.asp?txtCodeId=4422&lngWId=3
/*


#include	"stdafx.h"
#include	"resource.h"
#include	<windows.h>
#include	"Commdlg.h" // <-- used for OPENFILE DIALOG procedures, link with comdlg32
#include	<stdio.h>

#include	<stdlib.h>
#include	<string.h> 
#include	<time.h> 
#include	<winsock.h>
#include	<math.h>

#include	<stdio.h>
#include	<stdlib.h> 
#include	<fstream.h>

#include	"Time.h"


#include	<mmsystem.h> // for waveout stuff . link with winmm.lib
#include	<commctrl.h> // for progress bar animation, link with COMCTL32.LIB 



////////////////////// defines ////////////////////////////////////////////////////
// #define		      APP_PORT                      0x4000
// #define			APP_PORT				4000
// #define			PACKET_SIZE				1280
// #define			MAX_PACKET_SIZE			PACKET_SIZE + sizeof(rtp_hrt_t) 
// #define			BLOCK_SIZE				PACKET_SIZE * 8
// #define			BLOCK_COUNT				15


#define				WM_SOCKETREAD			(WM_APP + 100)


static volatile unsigned int Payload_Bytes_Received = 0; 
static int			APP_PORT;
static int			PACKET_SIZE;
static int			MAX_PACKET_SIZE; 
static int			BLOCK_SIZE;
static int 			BLOCK_COUNT;


/////////////////////////////////////////////////////////////////////////////////////
// STATIC 																	
// It simply means that once the variable has been initialized,						
// it remains in memory until the end of the program. 

static CRITICAL_SECTION			waveCriticalSection;
static WAVEHDR*					waveBlocks;
static volatile int				waveFreeBlockCount;	// volatile? -> Its value
static int						waveCurrentBlock;	// constantly changes..


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
	   unsigned long ssrc:32;    // source identification 
 //	   optional CSRC list 
 //    unsigned long csrc[1];  
	   SYSTEMTIME	servers_time;// not defined in RTP, we use it to calculate the end-to-end
								 // delay in packets' generation and death
} rtp_hrt_t;

///////////////////////////// RTP_PACKET //////////////////////////////////////////


typedef struct _RTP_PACKET {

	rtp_hrt_t				RTP_Header;
	char					Data[1536];
	int						payload_size;
	int						packet_size;

} RTP_PACKET;



// FIXED  buffers for reading ....
static char packet_buffer[1536];	// packet size + header should be lower than 1536 bytes
static		RTP_PACKET				*packet = new RTP_PACKET[sizeof(packet)];


// We launch this thread -- "SocketRead" -- to read the socket and play back audio when the 
// "WSAAsyncSelect" check box is NOT checked. By this way, we want to examine
// the effect of Microsoft's socket notication events.
// "WSAAsyncSelect" is a non-blocking socket while our thread is a socket blocking thread.
///////////////////////////////////////////////////////////////////////////////////
DWORD WINAPI SocketRead(LPVOID lParam);  // main thread to read + play back packets.
///////////////////////////////////////////////////////////////////////////////////



////////////////////// SoundDialog  CLASS//////////////////////////////////////////

class SoundDialog {
public:

		HWAVEOUT			hWaveOut;				// handle to playback device	
		HWND				DialogHandle;			// handle to Dialog window
		
		SOCKET				my_socket;
		struct sockaddr_in	SockAddr;
		
		HWND				ProgressBar;

		WAVEFORMATEX		wfx;
		
		unsigned int		bytes_received;
		unsigned int		secs_passed;
		unsigned int		myTimerEvent;
		bool				Sound_Device_Closing;	// playback device is closed/opened
		bool				debug_view;
		bool				launch_socket_thread;	// if we are not using WSAAsyncSelect,
													// we launch our own socket reading thread.
	
		HANDLE				hMutexStopButton;
		bool				StopButton;

		HANDLE				hThread;				// our own socket reading thread to start 											
		DWORD				dwThreadId;

		fstream 			outfile;				// that's for outputting info
		fstream				outfile2;				// that's for end-to-end playout delay


		// for packet stats;

		unsigned int		last_seq;		//=0;
		struct				timeval last_valid;		// Time last valid packet was received
		unsigned long		total_duration;	// =0;
		unsigned long		total_duration_end_to_end;
		unsigned long		packet_count_before_check;
		unsigned long		packet_count; //=1;
		int					last_usec; // =0;
		int					packets_lost; // = 0;




////////////////// CLASS METHODS ///////////////////////////////////////////////////

int SoundDialog::gettimeofday(struct timeval* tp) {
    DWORD t;
    t = timeGetTime();
    tp->tv_sec = t / 1000;
    tp->tv_usec = t % 1000;
    // 0 indicates that the call succeeded. 
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////

// Connecting,  creating socket, and allocating blocks ..

//////////////////////////////////////////////////////////////////////////////////////////

void   SoundDialog::OnConnect(SoundDialog *pParam)
{
	
	SoundDialog* pDlg=(SoundDialog*) pParam;

	pDlg->launch_socket_thread = true;


	BOOL		bSuccess;
	int			Int_To_Copy;
	int			SamplesPerSecond, BitsPerSample, Channels;
	int			number_of_packets;
	MMRESULT	mmResult;

	LRESULT		LResult;
	HWND		hWndControl;
	



	pDlg->last_seq =		0;		
	pDlg->total_duration =  0;
	pDlg->total_duration_end_to_end = 0;
	pDlg->packet_count =    0;
	pDlg->packet_count_before_check = 0; // we don't sequence; just count 'em as they come in
	pDlg->last_usec =		0;
	pDlg->packets_lost =	0;
	pDlg->secs_passed  =    0;


	//	unsigned long	AddrIP;		
	//	WAVEFORMATEX	wfx;
		
	Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_Local_Port,&bSuccess,FALSE);
	if (bSuccess) 
		APP_PORT = Int_To_Copy;


	Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_Channels,&bSuccess,FALSE);
	if (bSuccess) 
		Channels = Int_To_Copy;
		

	Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_Samples_Per_Second,&bSuccess,FALSE);
	if (bSuccess) 
	    SamplesPerSecond = Int_To_Copy;
	
	Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_Bits_Per_Sample,&bSuccess,FALSE);
	if (bSuccess) 
		BitsPerSample = Int_To_Copy;



	hWndControl = GetDlgItem(pDlg->DialogHandle,IDC_DEBUG_VIEW);

	LResult = SendMessage((HWND) hWndControl,		// get state of the check button
			  (UINT) BM_GETSTATE,					// we need it to set the debug view 
			  (WPARAM) 0,      
			  (LPARAM) 0);

	if (LResult == BST_CHECKED)						// look inside the thread on how it
			pDlg->debug_view = true;				// works.
	else if (LResult == BST_UNCHECKED)
			pDlg->debug_view = false;



	hWndControl = GetDlgItem(pDlg->DialogHandle,IDC_WSAAsyncSelect);

	LResult = SendMessage((HWND) hWndControl,	    // get state of the check button
			  (UINT) BM_GETSTATE,					// we need it to set socket type
			  (WPARAM) 0,      
			  (LPARAM) 0);

	if (LResult == BST_CHECKED) 
			pDlg->launch_socket_thread = false;
	else if (LResult == BST_UNCHECKED)
			pDlg->launch_socket_thread = true;

		

	//	AddrIP = inet_addr(IPAddress);
	//	if (AddrIP != INADDR_NONE)
	//		memcpy(&(SockAddr.sin_addr),&AddrIP,sizeof(SockAddr.sin_addr))
		
	// set up the WAVEFORMATEX structure.
    pDlg->wfx.nSamplesPerSec  = SamplesPerSecond;  // sample rate 
    pDlg->wfx.wBitsPerSample  = BitsPerSample;     // sample size 
    pDlg->wfx.nChannels       = Channels;          // channels
    
    pDlg->wfx.cbSize          = 0;      // size of _extra_ info 
    pDlg->wfx.wFormatTag      = WAVE_FORMAT_PCM;
    pDlg->wfx.nBlockAlign     = (wfx.wBitsPerSample * wfx.nChannels) >> 3;
    pDlg->wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;

   //   try to open the default wave device. WAVE_MAPPER is
   //   a constant defined in mmsystem.h, it always points to the
   //   default wave device on the system (some people have 2 or
   //   more sound cards).

   //   that's if we are using a callback function
   //   The 5th parameter has no effect if we are using a CALLBACK_WINDOW 
   //	mmResult = waveOutOpen(&hWaveOut,(UINT)WAVE_MAPPER,
   //	(LPWAVEFORMATEX)&wfx,(DWORD)DialogHandle,(DWORD)&waveFreeBlockCount,CALLBACK_WINDOW);
	mmResult = waveOutOpen(&pDlg->hWaveOut,(UINT)WAVE_MAPPER,
		(LPWAVEFORMATEX)&pDlg->wfx,(DWORD)pDlg->DialogHandle,(DWORD)NULL,CALLBACK_WINDOW);

	if (mmResult !=MMSYSERR_NOERROR) 
		{ 
			MessageBox(DialogHandle,"Can't open wave call back device","Error!!",MB_OK );
			return;

		}
	
		// now start building the bind structure
		// and build all the utp network stuff..
		ZeroMemory(&pDlg->SockAddr,sizeof(pDlg->SockAddr));
		
		pDlg->my_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

		pDlg->SockAddr.sin_family		= AF_INET;
 
		pDlg->SockAddr.sin_addr.s_addr  = INADDR_ANY;	// use this computer's IP
 
		pDlg->SockAddr.sin_port			= htons(APP_PORT);

		bind(pDlg->my_socket, (SOCKADDR *)&pDlg->SockAddr,sizeof(pDlg->SockAddr));	


		// make the socket asynchronous so we can receive packets while playing audio
		if (pDlg->launch_socket_thread == false)
			WSAAsyncSelect(pDlg->my_socket,pDlg->DialogHandle, WM_SOCKETREAD, FD_READ);

		Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_PacketPayload,&bSuccess,FALSE);
		if (bSuccess) 
			PACKET_SIZE = Int_To_Copy;

		
		MAX_PACKET_SIZE = PACKET_SIZE + sizeof(rtp_hrt_t);

		Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_BlockSize,&bSuccess,FALSE);
		if (bSuccess) 
			number_of_packets = Int_To_Copy;
				
		BLOCK_SIZE = PACKET_SIZE * number_of_packets;

		Int_To_Copy = GetDlgItemInt(pDlg->DialogHandle,IDC_BlockCount,&bSuccess,FALSE);
		if (bSuccess) 
			BLOCK_COUNT = Int_To_Copy;

		
		EnableWindow(GetDlgItem(pDlg->DialogHandle,IDC_Quit),FALSE);
		EnableWindow(GetDlgItem(pDlg->DialogHandle,IDC_CONNECT),FALSE);
		EnableWindow(GetDlgItem(pDlg->DialogHandle,IDC_STOP),TRUE);


		InitializeCriticalSection(&waveCriticalSection);

		waveBlocks         = pDlg->allocateBlocks(BLOCK_SIZE, BLOCK_COUNT);
		waveFreeBlockCount = BLOCK_COUNT;
		waveCurrentBlock   = 0;


		// used for the progress bar.. 
		InitCommonControls(); 	
		ProgressBar = GetDlgItem(pDlg->DialogHandle,IDC_PROGRESS2);
		SendMessage(ProgressBar, PBM_SETRANGE, 0, MAKELPARAM(1, BLOCK_COUNT));
		SendMessage(ProgressBar, PBM_SETSTEP, (WPARAM) 1, 0); 

	
			
		// MAKE THE TIMER TICK EVERY 1 SEC, CALCULATE THE BYTES RECEIVED..
		SetTimer(pDlg->DialogHandle,pDlg->myTimerEvent,1000,NULL);
		
		// create mutex for the shared "button" source. 
		// "Disconnect" is shared between the dialog's
		// thread and the thread we launched.
		pDlg->hMutexStopButton = CreateMutex(NULL,0,NULL);
		WaitForSingleObject(pDlg->hMutexStopButton,INFINITE);
		pDlg->StopButton = false;
		ReleaseMutex(pDlg->hMutexStopButton);


//		pDlg->outfile.open("client_data.txt", ios::out);
//		pDlg->outfile << "Throughput/Sec Packets_Lost Total_Packet_Count
//			Total_Duration_msec Total_duration/Packet_count Difference Free_Memory"  << "\n";

//		pDlg->outfile2.open("playout_delay.txt", ios::out);



		if (pDlg->launch_socket_thread == true) 
		{

			pDlg->hThread = CreateThread(
			NULL,							// default security attributes 
			0,								// use default stack size  
			SocketRead,						// thread function 
			pDlg,							// pass pDlg so we can have access to the class 
			0,								// use default creation flags 
			&pDlg->dwThreadId);	
		
		}

		return;


}


////////////////// SoundDialog::OnStop ///////////////////////////////////////////////////////



void SoundDialog::OnStop(SoundDialog *pParam)
{

	SoundDialog* pDlg=(SoundDialog*) pParam;
	HWND	hWndEdit;


//	if (( pDlg->outfile.is_open() ) )		// close the file anyway
//		pDlg->outfile.close();

//		if (( pDlg->outfile2.is_open() ) )	// close the file anyway
//		pDlg->outfile2.close();


	
	if (pDlg->hWaveOut != 0)				// if sound device is open 
	{	


	WaitForSingleObject(pDlg->hMutexStopButton,INFINITE);
	pDlg->StopButton = true;
	ReleaseMutex(pDlg->hMutexStopButton);

	hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_SOUND_STATUS);
	SendMessage(hWndEdit,EM_SETSEL,0,256000);
	SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)"Sound Card Closed");

	EnableWindow(GetDlgItem(pDlg->DialogHandle,IDC_Quit),TRUE);
	EnableWindow(GetDlgItem(pDlg->DialogHandle,IDC_CONNECT),TRUE);
	EnableWindow(GetDlgItem(pDlg->DialogHandle,IDC_STOP),FALSE);


		if (pDlg->launch_socket_thread == false )	// we are using WSSAsyncSelect
		{											
	
			//disable socket notification 
			WSAAsyncSelect(pDlg->my_socket,pDlg->DialogHandle, 0, 0);

			closesocket(pDlg->my_socket);

			CloseHandle(pDlg->hMutexStopButton);

			KillTimer(pDlg->DialogHandle,pDlg->myTimerEvent);
		
			waveOutReset(pDlg->hWaveOut);
			waveOutClose(pDlg->hWaveOut);

		//	while (waveFreeBlockCount < BLOCK_COUNT)  { // this locks the program
		//			Sleep(10);							// need to fix it..
		//		}

		//	waveFreeBlockCount = BLOCK_COUNT;

		for (int i = 0; i < BLOCK_COUNT; i++) 
			if(waveBlocks[i].dwFlags & WHDR_PREPARED)
				waveOutUnprepareHeader(pDlg->hWaveOut, &waveBlocks[i], sizeof(WAVEHDR));

			pDlg->freeBlocks(waveBlocks);			
		
	
		

//			  pDlg->CleanUp(pDlg); <-- this doen't work for OnSocketRead :-((

			// DeleteCriticalSection(&waveCriticalSection); // this crashes. need to fix it ?
			// Partial fix: When we hit "Quit", "DeleteCriticalSection(&waveCriticalSection)" 
			// works fine. Otherwise, it crashes :-((


		}	//  if (pDlg->launch_socket_thread == false )

	}// 	if (pDlg->hWaveOut != 0)		


	return;

}

//////////////////////////////////////////////////////////////////////////////////////////


bool SoundDialog::OnInit(HWND hWnd) 
{

		DialogHandle	=	hWnd;
		hWaveOut		=	0;		
		bytes_received	=   0;

		return true;

}


// when block is done playing increase the waveFreeBlockCount variable
//////////////////////////////////////////////////////////////////////////////////////////////
void SoundDialog::OnWomDone(SoundDialog *pDialog)
{

  SoundDialog* pDlg=(SoundDialog*) pDialog;

  EnterCriticalSection(&waveCriticalSection);
  waveFreeBlockCount++;		// here we increase the value

  SetDlgItemInt(pDlg->DialogHandle,IDC_Status,waveFreeBlockCount,FALSE);
  SendMessage(ProgressBar,(UINT) PBM_SETPOS,(WPARAM) waveFreeBlockCount,0);
  
  LeaveCriticalSection(&waveCriticalSection);

  return;
}

///////////////////////////////////////////////////////////////////////////////

void SoundDialog::OnWomClose(SoundDialog *pParam)
{

	HWND	hWndEdit;

	SoundDialog* pDlg= (SoundDialog*) pParam;	


	hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_SOUND_STATUS);
	SendMessage(hWndEdit,EM_SETSEL,0,256000);
	SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)"Sound Card Closed");
		
	pDlg->hWaveOut = 0;
	
	return;

}

/////////////////////////////////////////////////////////////////////////////////

void SoundDialog::OnWomOpen(SoundDialog *pParam)
{
	
	HWND	hWndEdit;

	SoundDialog* pDlg= (SoundDialog*) pParam;	

	hWndEdit = GetDlgItem(pDlg->DialogHandle,IDC_SOUND_STATUS);
	SendMessage(hWndEdit,EM_SETSEL,0,256000);
	SendMessage(hWndEdit,EM_REPLACESEL,FALSE,(long)"Sound Card Opened");
	
	return;

}


/////////////////////////////////////////////////////////////////////////////////

void SoundDialog::UpdateThroughput(SoundDialog *pDialog)
{


	SoundDialog* pDlg = (SoundDialog*) pDialog;

	SetDlgItemInt(pDlg->DialogHandle,ID_Timer,pDlg->bytes_received - Payload_Bytes_Received,FALSE);

	SetDlgItemInt(pDlg->DialogHandle,IDC_SecondsPassed,pDlg->secs_passed,FALSE);

	pDlg->secs_passed = pDlg->secs_passed + 1;


   /*
	if ( !pDlg->outfile2.is_open() ) {
		// The file could not be opened
		}
		else 
		{
		
		pDlg->outfile2 << pDlg->bytes_received - Payload_Bytes_Received << " ";
		pDlg->outfile2 << pDlg->packets_lost << " ";
		pDlg->outfile2 << pDlg->packet_count << " ";
//		pDlg->outfile << pDlg->total_duration << " ";
		
		if (pDlg->packet_count != 0)
			pDlg->outfile2 << (float)(pDlg->total_duration / pDlg->packet_count) << " ";
		
//		pDlg->outfile << (pDlg->last_valid.tv_usec - pDlg->last_usec) << " ";
		pDlg->outfile2 << waveFreeBlockCount << " " ;
		pDlg->outfile2 << pDlg->total_duration_end_to_end/pDlg->packet_count_before_check;
		pDlg->outfile2 << "\n";


		}

	*/

 
		Payload_Bytes_Received = pDlg->bytes_received;

	return;
}



///////////////////////////////////////////////////////////////////////////////////////////
// allocate sound blocks
 
static		WAVEHDR*	SoundDialog::allocateBlocks(int size, int count)
{

	unsigned char* buffer;
    int i;
    WAVEHDR* blocks;
    DWORD totalBufferSize = (size + sizeof(WAVEHDR)) * count;
  
    //  allocate memory for the entire set in one go
    if((buffer = (unsigned char*)HeapAlloc(
        GetProcessHeap(), 
        HEAP_ZERO_MEMORY, 
        totalBufferSize
    )) == NULL) {
		MessageBox(NULL,"Memory Allocation Error (HeapAlloc)", "Mem Error", NULL);
        ExitProcess(1);
    }

    
    //  and set up the pointers to each bit
    blocks = (WAVEHDR*)buffer;
	buffer = buffer + sizeof(WAVEHDR) * count;

    for(i = 0; i < count; i++) {
        blocks[i].dwBufferLength = size;
        blocks[i].lpData = (char*)buffer;
        buffer = buffer + size;

    }
    

    return blocks;
}



//////////////////////////////////////////////////////////////////////////////////////
// free allocated blocks for audio 
static void freeBlocks(WAVEHDR* blockArray)
{
     
    // and this is why allocateBlocks works the way it does  | David Overton     
    HeapFree(GetProcessHeap(), 0, blockArray);
}



/////////////////////////////////////////////////////////////////////////////////////////////////

// Note here: when "static" is specified with a variable inside a function, it allows
// the variable to retain its value between calls to the function.
static void SoundDialog::writeAudio(HWAVEOUT hWaveOut, LPSTR data, int size)
{
	// filling blocks of audio and sending them to sound card
    WAVEHDR* current;
    int remain;

    current = &waveBlocks[waveCurrentBlock];
    

    while(size > 0) 
	{
         

        //  first make sure the header we're going to use is unprepared
		
        if(current->dwFlags & WHDR_PREPARED) 
            waveOutUnprepareHeader(hWaveOut, current, sizeof(WAVEHDR));
		
        if(size < (int)(BLOCK_SIZE - current->dwUser)) 
		{
            memcpy(current->lpData + current->dwUser, data, size);
            current->dwUser = current->dwUser + size; 	
			break;
        }
		
        remain = BLOCK_SIZE - current->dwUser;
        memcpy(current->lpData + current->dwUser, data, remain);
        size = size - remain;
        data = data + remain;

        current->dwBufferLength = BLOCK_SIZE;      
	
        waveOutPrepareHeader(hWaveOut, current, sizeof(WAVEHDR));
	    waveOutWrite(hWaveOut, current, sizeof(WAVEHDR));
		
		EnterCriticalSection(&waveCriticalSection);
		waveFreeBlockCount--;
		LeaveCriticalSection(&waveCriticalSection);
     
   
        //  wait for a block to become free        
        while(!waveFreeBlockCount) //	// we may loose packets here..
		{
            Sleep(10);
        }

       //   point to the next block		
        waveCurrentBlock++;
        waveCurrentBlock %= BLOCK_COUNT;

        current = &waveBlocks[waveCurrentBlock];
        current->dwUser = 0;
    	
		}

}



///////////////////////// OnQuit /////////////////////////////////////////////////////

void SoundDialog::OnQuit(SoundDialog *pParam)
{
	
		SoundDialog* pDlg = (SoundDialog*) pParam;

		if (pDlg->launch_socket_thread == false)  												  	
			DeleteCriticalSection(&waveCriticalSection);

//		if ( ( pDlg->outfile.is_open() ) )
//			pDlg->outfile.close();

//			if ( ( pDlg->outfile2.is_open() ) )
//			pDlg->outfile2.close();

}


///////////////////////////////////////////////////////////////////////////////////////
// used when we are receiving through our global thread..
///////////////////////////////////////////////////////////////////////////////////////
void CleanUp(SoundDialog *pParam)
{

	SoundDialog* pDlg = (SoundDialog*) pParam;
	
//	if (( pDlg->outfile.is_open() ) )
//		pDlg->outfile.close();

//	if (( pDlg->outfile2.is_open() ) )
//		pDlg->outfile2.close();



	if (pDlg->hWaveOut !=0 )
	{
		waveOutReset(pDlg->hWaveOut);
		waveOutClose(pDlg->hWaveOut);



	if (pDlg->launch_socket_thread== true)
	{	
		while (waveFreeBlockCount < BLOCK_COUNT)  
		{
			Sleep(10);
		}

	}


	for (int i = 0; i < BLOCK_COUNT; i++) 
        if(waveBlocks[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(pDlg->hWaveOut, &waveBlocks[i], sizeof(WAVEHDR));


	DeleteCriticalSection(&waveCriticalSection);

    pDlg->freeBlocks(waveBlocks);

	
	}

}



///////////////////// SoundDialog::OnSocketRead /////////////////////////////////////

// use this function when we have declared our socket with WSAAsyncSelect non-blocking
// WSAAsyncSelect itself creates its own thread.
void SoundDialog::OnSocketRead(SoundDialog *pParam)
{
		
	SoundDialog* pDlg=(SoundDialog*) pParam;

	char str[32];
//	char stimeresult[100];

	int SenderAddrSize	= sizeof(SockAddr);	
	int bytes_returned = 0;
	
//	SYSTEMTIME				thisTime;		 // used to hold current time;

	//	Time					*serversTime,*clientsTimes,*minusTimePtr;


	//	if (waveFreeBlockCount < 2 )	// somehow it crashes we have not  enough blocks to write 
	//		return;


	//  wait for a block to become free        
        while(!waveFreeBlockCount) 
		{
			MessageBox(NULL,"sleep","waiting for free block..",NULL);

            Sleep(10);
        }

	bytes_returned	= recvfrom(my_socket,
							(char*)packet_buffer,
							(size_t)MAX_PACKET_SIZE,
							0,
							(SOCKADDR*)&SockAddr,
							&SenderAddrSize);	

	// copying data from the socket into the RTP instance
	if (bytes_returned > 0)
	{
	
//	pDlg->packet_count_before_check++;		// as bytes come in, use them to calculate
											// end-to-end delay. Don't include out of sequence
											// restrictions


	packet->payload_size = bytes_returned - sizeof(rtp_hrt_t);
	packet->packet_size  = bytes_returned;
	
	// decode network byte order etc...
	packet->RTP_Header.seq =  ntohs( packet->RTP_Header.seq );	
	packet->RTP_Header.ts  =  ntohl( packet->RTP_Header.ts  );

	memcpy( &packet->RTP_Header, packet_buffer, sizeof(rtp_hrt_t) ); 
	memcpy( (char*) &packet->Data,
					packet_buffer + sizeof(rtp_hrt_t),
					(size_t)packet->payload_size);

	pDlg->bytes_received = pDlg->bytes_received + packet->payload_size;

	///////////////////////////////////////////////////////////////////////////////////
	// we calcute the end-to-end packet delay using these classes:

	/*

	serversTime = new Time( (unsigned short)(packet->RTP_Header.servers_time.wHour),
					(unsigned short)(packet->RTP_Header.servers_time.wMinute),
					(unsigned short)(packet->RTP_Header.servers_time.wSecond),
					(unsigned short)(packet->RTP_Header.servers_time.wMilliseconds) );
					
	GetSystemTime(&thisTime);	// Get the system time of this computer's 

	clientsTimes = new Time ((unsigned short)thisTime.wHour,
					(unsigned short)thisTime.wMinute,
					(unsigned short)thisTime.wSecond,
					(unsigned short)thisTime.wMilliseconds);
	
	minusTimePtr =  (*clientsTimes)-(*serversTime);  // this operator (-) has been overloaded...
													// see more into "Time.h"
	
	sprintf(stimeresult, "%02d:%02d:%02d:%03d\0",
		minusTimePtr->hours,minusTimePtr->minutes,
		minusTimePtr->seconds,minusTimePtr->milliseconds);

		pDlg->total_duration_end_to_end = pDlg->total_duration_end_to_end + 
											abs(minusTimePtr->milliseconds);
	if (pDlg->packet_count_before_check > 0)
		SetDlgItemInt(pDlg->DialogHandle,IDC_Mean_End_To_End,
		pDlg->total_duration_end_to_end/pDlg->packet_count_before_check, FALSE);


		delete serversTime;
		delete clientsTimes;
		delete minusTimePtr;

	*/


	///////////////////////////////////////////////////////////////////////////////////

	// first check for sequence/missing packets,
	if (pDlg->last_seq && pDlg->last_seq != packet->RTP_Header.seq - 1 && pDlg->last_seq!=65535) 
	{
				
		int diff = abs(packet->RTP_Header.seq - pDlg->last_seq);
		int samples = diff * (PACKET_SIZE / pDlg->wfx.nBlockAlign); // pcm 16 bit etc..

		pDlg->packets_lost += diff;
		
		sprintf(&str[0],"%u",packet->RTP_Header.seq);	
		SetDlgItemText(pDlg->DialogHandle,IDC_this,str);

		sprintf(&str[0],"%u",pDlg->last_seq);	
		SetDlgItemText(pDlg->DialogHandle,IDC_last,str);
	
		sprintf(&str[0],"%u",diff);	
		SetDlgItemText(pDlg->DialogHandle,IDC_diff,str);
	
		sprintf(&str[0],"%fs",samples * ( (float)1/44100) );
		SetDlgItemText(pDlg->DialogHandle,IDC_lost,str);

//		sprintf(&str[0],"%u",packet->RTP_Header.ts);				
//		SetDlgItemText(pDlg->DialogHandle,IDC_TimeStamp,str);

		sprintf(&str[0],"%u",packet->RTP_Header.seq);			
		SetDlgItemText(pDlg->DialogHandle,IDC_Sequence,str);

		sprintf(&str[0],"%d",pDlg->packets_lost);			
		SetDlgItemText(pDlg->DialogHandle,IDC_lost_packets,str);
		

		// write audio even if we are out of sequence...
		writeAudio(hWaveOut,(char*)&packet->Data,packet->payload_size);

	} // if (last_seq && last_seq != packet->RTP_Header.seq - 1 && last_seq!=65535) 
	
	else 
	
	{
			// Store the time of this successful packet 
			gettimeofday(&pDlg->last_valid);			

// origical code by Nick Humprey:			
//	if (last_seq < packet->head.seq || last_seq==65535)
//		fprintf(stderr, "%i.%i: payload_size=%d ts=%u seq=%u diff=%d avrg=%d\n", 
//		(int)last_valid.tv_sec, (int)last_valid.tv_usec, 
//		packet->payload_size, packet->head.ts, packet->head.seq,
//		(int)last_valid.tv_usec-last_usec, (int)(total_duration/packet_count));


			if (pDlg->debug_view) // here's why we set the debug view			
			{						
//			sprintf(&str[0],"%i",(int)pDlg->last_valid.tv_sec);	
//			SetDlgItemText(pDlg->DialogHandle,IDC_LastValid_msec,str);	
//			sprintf(&str[0],"%i",(int)pDlg->last_valid.tv_usec);		
//			SetDlgItemText(pDlg->DialogHandle,IDC_LastValid_usec,str);
//			sprintf(&str[0],"%u",packet->RTP_Header.ts);				
//			SetDlgItemText(pDlg->DialogHandle,IDC_TimeStamp,str);

			sprintf(&str[0],"%d",packet->payload_size);				
			SetDlgItemText(pDlg->DialogHandle,IDC_PayloadSize,str);

			sprintf(&str[0],"%u",packet->RTP_Header.seq);			
			SetDlgItemText(pDlg->DialogHandle,IDC_Sequence,str);
			
			sprintf(&str[0],"%d",(int)(pDlg->last_valid.tv_usec - pDlg->last_usec) );	
			SetDlgItemText(pDlg->DialogHandle,IDC_Difference,str );
			
			if (pDlg->packet_count != 0) {
			sprintf(&str[0],"%d",(int)(pDlg->total_duration / pDlg->packet_count));		
			SetDlgItemText(pDlg->DialogHandle,IDC_Average,str);
			}

//			sprintf(&str[0],"%d",pDlg->total_duration);		
//			SetDlgItemText(pDlg->DialogHandle,IDC_totalDuration,str);
//			sprintf(&str[0],"%u",packet->RTP_Header.seq);	
//			SetDlgItemText(pDlg->DialogHandle,IDC_last_successful,str);
			
//			SetDlgItemText(pDlg->DialogHandle,IDC_EndToEndDelay,stimeresult);


			} // end debug view..

			if ( (pDlg->last_valid.tv_usec - pDlg->last_usec ) > 0) 
			{
					pDlg->total_duration += (pDlg->last_valid.tv_usec-pDlg->last_usec);
					pDlg->packet_count++;
					SetDlgItemInt(pDlg->DialogHandle,
						IDC_PacketReceived,pDlg->packet_count,
						FALSE);
			
			}
		

			pDlg->last_usec = last_valid.tv_usec;

		}  // else // in case we have lost packet/frames, we play them twice ..
	
	
		//	pDlg->outfile2 << stimeresult;
		//	pDlg->outfile2 << "\n";
		// Output audio, send it to the audio card
		writeAudio(hWaveOut,(char*)&packet->Data,packet->payload_size);
	
		last_seq = packet->RTP_Header.seq;

		} // if bytes returned > 0


	}


}; 

// END CLASS METHODS AND DEFINITIONS  



//////////////////////////// GLOBAL FUNCTIONS /////////////////////////////////////////////



// our main thread for blocking-socket reading.
DWORD WINAPI SocketRead(LPVOID lParam)
{
	

	 SoundDialog* pDlg = (SoundDialog*) lParam;

	 bool LocalStopButton = false;
	
//	 SYSTEMTIME				thisTime;		// used to hold current time;
	 		 										   // delay...	 
	 long int				packets_received = 0;

                             
	 int SenderAddrSize	= sizeof(pDlg->SockAddr);	
	 int bytes_returned = 0;					
	
	 char str[32];
//	 char stimeresult[100];

	// GetSystemTime(&stime);
	// sprintf(sresult, "%02i-%02i-%0004i %02i:%02i:%02i\0", stime.wDay, stime.wMonth, stime.wYear, stime.wHour, stime.wMinute, stime.wSecond);

	 memset(packet_buffer,0,sizeof(packet_buffer) );
	 memset(packet,0,sizeof(packet) );


	 //loop forever until we hit the "disconnect button"
	while (!LocalStopButton)		
	{
	bytes_returned	= recvfrom(pDlg->my_socket,
						(char*)packet_buffer,
						(size_t)MAX_PACKET_SIZE, 0,
						(SOCKADDR*)&pDlg->SockAddr,
						&SenderAddrSize);
	
	if (bytes_returned > 0) 
	{	

	//	pDlg->packet_count_before_check++;
	//	SetDlgItemInt(pDlg->DialogHandle,IDC_PacketReceived,pDlg->packet_count_before_check,FALSE);
		
	// copying data from the socket into the RTP instance

	packet->payload_size = bytes_returned - sizeof(rtp_hrt_t);
	packet->packet_size  = bytes_returned;
	

	// decode network byte order etc...
	packet->RTP_Header.seq =  ntohs( packet->RTP_Header.seq );	
	packet->RTP_Header.ts  =  ntohl( packet->RTP_Header.ts  );

	memcpy( &packet->RTP_Header, (char*)packet_buffer, sizeof(rtp_hrt_t) ); 

	memcpy( (char*) &packet->Data, packet_buffer + sizeof(rtp_hrt_t), (size_t)packet->payload_size);

	pDlg->bytes_received = pDlg->bytes_received + packet->payload_size;


	/*  this does not work as expected. Don' include the calculations

		Time *serversTime,*clientsTime,*minusTimePtr;

	// we calcute the end-to-end packet delay using these classes:
	serversTime = new Time((unsigned short)packet->RTP_Header.servers_time.wHour,
					(unsigned short)packet->RTP_Header.servers_time.wMinute,
					(unsigned short)packet->RTP_Header.servers_time.wSecond,
					(unsigned short)packet->RTP_Header.servers_time.wMilliseconds);
					
	GetSystemTime(&thisTime);

	clientsTime = new Time((unsigned short)thisTime.wHour,
					(unsigned short)thisTime.wMinute,
					(unsigned short)thisTime.wSecond,
					(unsigned short)thisTime.wMilliseconds);
	
		minusTimePtr =  (*clientsTime)-(*serversTime);  // this operator (-) has been overloaded...
														// see more into "Time.h"
	
		sprintf(stimeresult, "%02d:%02d:%02d:%03d\0",
		minusTimePtr->hours,minusTimePtr->minutes,
		minusTimePtr->seconds,minusTimePtr->milliseconds);

		pDlg->total_duration_end_to_end = pDlg->total_duration_end_to_end + 
											abs(minusTimePtr->milliseconds);
	if (pDlg->packet_count_before_check > 0)
		SetDlgItemInt(pDlg->DialogHandle,IDC_Mean_End_To_End,
		pDlg->total_duration_end_to_end/pDlg->packet_count_before_check, FALSE);

		delete clientsTime;
		delete serversTime;
		delete minusTimePtr;

	*/



	// check here if packets are are out of sequence
	if (pDlg->last_seq && pDlg->last_seq != packet->RTP_Header.seq - 1 && pDlg->last_seq!=65535) 
	
	{
		
			
		int diff = abs(packet->RTP_Header.seq - pDlg->last_seq);
		int samples = diff * (PACKET_SIZE / pDlg->wfx.nBlockAlign); // pcm 16 bit etc..

		pDlg->packets_lost += diff;

//		sprintf(&str[0],"%u",packet->RTP_Header.seq);	
//		SetDlgItemText(pDlg->DialogHandle,IDC_this,str);
		SetDlgItemInt(pDlg->DialogHandle,IDC_this,packet->RTP_Header.seq,FALSE);

		sprintf(&str[0],"%u",pDlg->last_seq);	
		SetDlgItemText(pDlg->DialogHandle,IDC_last,str);
	
		sprintf(&str[0],"%u",diff);	
		SetDlgItemText(pDlg->DialogHandle,IDC_diff,str);
	
		sprintf(&str[0],"%fs",samples * ( (float)1/44100) );
		SetDlgItemText(pDlg->DialogHandle,IDC_lost,str);

		sprintf(&str[0],"%u",packet->RTP_Header.ts);				
		SetDlgItemText(pDlg->DialogHandle,IDC_TimeStamp,str);

		sprintf(&str[0],"%u",packet->RTP_Header.seq);			
		SetDlgItemText(pDlg->DialogHandle,IDC_Sequence,str);

		sprintf(&str[0],"%d",pDlg->packets_lost);			
		SetDlgItemText(pDlg->DialogHandle,IDC_lost_packets,str);
		
		// write audio
		pDlg->writeAudio(pDlg->hWaveOut,(char*)&packet->Data,packet->payload_size);


		} // if (last_seq && last_seq != packet->RTP_Header.seq - 1 && last_seq!=65535) 
	
		else 
	
		{	// no packets out of sequence..


			// Store the time of this successful packet 
			pDlg->gettimeofday(&pDlg->last_valid);

			
			if (pDlg->debug_view) {

//			sprintf(&str[0],"%i",(int)pDlg->last_valid.tv_sec);	
//			SetDlgItemText(pDlg->DialogHandle,IDC_LastValid_msec,str);
	
//			sprintf(&str[0],"%i",(int)pDlg->last_valid.tv_usec);		
//			SetDlgItemText(pDlg->DialogHandle,IDC_LastValid_usec,str);

			sprintf(&str[0],"%d",packet->payload_size);				
			SetDlgItemText(pDlg->DialogHandle,IDC_PayloadSize,str);

//			sprintf(&str[0],"%u",packet->RTP_Header.ts);				
//			SetDlgItemText(pDlg->DialogHandle,IDC_TimeStamp,str);

			sprintf(&str[0],"%u",packet->RTP_Header.seq);			
			SetDlgItemText(pDlg->DialogHandle,IDC_Sequence,str);
			
			sprintf(&str[0],"%d",(int)(pDlg->last_valid.tv_usec-pDlg->last_usec) );	
			SetDlgItemText(pDlg->DialogHandle,IDC_Difference,str );

			if (pDlg->packet_count != 0) {
			sprintf(&str[0],"%d",(int)(pDlg->total_duration/pDlg->packet_count));		
			SetDlgItemText(pDlg->DialogHandle,IDC_Average,str);
			}

//			SetDlgItemText(pDlg->DialogHandle,IDC_EndToEndDelay,stimeresult);

//			sprintf(&str[0],"%d",pDlg->total_duration);		
//			SetDlgItemText(pDlg->DialogHandle,IDC_totalDuration,str);

//			sprintf(&str[0],"%u",packet->RTP_Header.seq);	
//			SetDlgItemText(pDlg->DialogHandle,IDC_last_successful,str);
			

			if ( (pDlg->last_valid.tv_usec - pDlg->last_usec ) > 0)  {
					pDlg->total_duration += (pDlg->last_valid.tv_usec-pDlg->last_usec);
					pDlg->packet_count++;
					SetDlgItemInt(pDlg->DialogHandle,
						IDC_PacketReceived,pDlg->packet_count,
						FALSE);


			} // if pDlg->last_valid.tv_usec - pDlg->last_usec ...)
					
				pDlg->last_usec =pDlg->last_valid.tv_usec;
			
			}	// end debug view..
		
		}  // else 

		// Write audio

		//	pDlg->outfile2 << (pDlg->last_valid.tv_usec - pDlg->last_usec) << " \n";
			
		//	pDlg->outfile2 << stimeresult;
		//	pDlg->outfile2 << "\n";

			pDlg->writeAudio(pDlg->hWaveOut, (char*)&packet->Data, packet->payload_size);
			
			pDlg->last_seq = packet->RTP_Header.seq;
		
		
		
		} // if (bytes_returned > 0) 


		// at the end of loop handle and route responses related to "disconnect button"
		// when we click "disconnect", break out of loop, stop receiving..
		WaitForSingleObject(pDlg->hMutexStopButton,INFINITE);
		LocalStopButton = pDlg->StopButton;
		ReleaseMutex(pDlg->hMutexStopButton);

		}  // 	while (!LocalStopButton) 


		CloseHandle(pDlg->hThread);
		CloseHandle(pDlg->hMutexStopButton);
	
		closesocket(pDlg->my_socket);
	
		KillTimer(pDlg->DialogHandle,pDlg->myTimerEvent);
		
		pDlg->CleanUp(pDlg);
		

		
		return 0;	

}

/////////////////////////////////////////////////////////////////////////////////////////

int CALLBACK DialogProc(HWND hDlg, UINT MSG, WPARAM wParam, LPARAM lParam)
{

	// STEP # 2	


	int i;

	SoundDialog	*pDialog;

	pDialog = (SoundDialog *) GetWindowLong(hDlg,DWL_USER);

	const char *PayloadValues[] = { "128", "256", "384",
                                "512", "640", "768",
                                "896", "1024"};

	const char *BlockSize[] = { "8","10","12","14",
								"16","18","20","22",
								"24","26","28","30" };

	const char *BlockCount[] = {"2","4","6","8",
								"10","12","14","16",
								"18","20" };


	const char *SamplesPerSecond[] = {"44100","22050","8000","11025"};
	const char *BitsPerSample[]    = {"16","8"};
	const char *Channels[]		   = {"2","1"};



	// for progress bar control
	// we need to initialize it...
	INITCOMMONCONTROLSEX InitCtrlEx;
	InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
	InitCtrlEx.dwICC  = ICC_PROGRESS_CLASS;
	InitCommonControlsEx(&InitCtrlEx);



	switch(MSG)
	{
	
		case WM_TIMER: 


		pDialog->UpdateThroughput((SoundDialog *) pDialog);

		break;  

		// sound card events ..

		case MM_WOM_DONE:
			pDialog->OnWomDone( (SoundDialog *) pDialog);
			break;

		case MM_WOM_OPEN:
				pDialog->OnWomOpen((SoundDialog *) pDialog);	
			break;

		case MM_WOM_CLOSE:
			pDialog->OnWomClose((SoundDialog *) pDialog);
			break;

	// that's if we using a non-blocking socket..
		case WM_SOCKETREAD:
			pDialog->OnSocketRead((SoundDialog *) pDialog);			
			break;			
			

	// handle cases for button clicks ......
		case WM_COMMAND:


					switch(LOWORD(wParam)) {
						case IDC_Quit:
							//	KillTimer(pDialog->DialogHandle,pDialog->myTimerEvent);										
								pDialog->OnQuit( (SoundDialog *) pDialog );
							// just close the window..
							EndDialog(hDlg, 0);

						break;
					
						case IDC_STOP:
							KillTimer(pDialog->DialogHandle,pDialog->myTimerEvent);	
							pDialog->OnStop((SoundDialog *) pDialog);							
							break;

						case IDC_CONNECT:
							pDialog->OnConnect((SoundDialog *) pDialog);
						break;
						}
						break;

		
			
		case WM_INITDIALOG:
			
			HWND	item;

			item = GetDlgItem(hDlg,IDC_PacketPayload);

			for (i = 0; i < 8; i++)  {
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)PayloadValues[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 7, 0);

		///////////////////////////////////////////////////////////////////////
		
			item = GetDlgItem(hDlg,IDC_BlockSize);

			for (i = 0; i < 10; i++)  {
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)BlockSize[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 6, 0);
		//////////////////////////////////////////////////////////////////////

		
			item = GetDlgItem(hDlg,IDC_BlockCount);

			for (i = 0; i < 10; i++)  {
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)BlockCount[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 9, 0);

		
			// Now fill up the values for the sound card.


			item = GetDlgItem(hDlg,IDC_Samples_Per_Second);

			for (i = 0; i < 4; i++)  {
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)SamplesPerSecond[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 0, 0);

			item = GetDlgItem(hDlg,IDC_Bits_Per_Sample);

			for (i = 0; i < 2; i++)  {
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)BitsPerSample[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 0, 0);

			item = GetDlgItem(hDlg,IDC_Channels);

			for (i = 0; i < 2; i++)  {
					   SendMessage(item,
                       (UINT)CB_INSERTSTRING,
                       (WPARAM)-1,
                        reinterpret_cast<LPARAM>((LPCTSTR)Channels[i]));
			}
			
			SendMessage(item, CB_SETCURSEL, 0, 0);


			EnableWindow(GetDlgItem(hDlg,IDC_Quit),TRUE);
			EnableWindow(GetDlgItem(hDlg,IDC_CONNECT),TRUE);
			EnableWindow(GetDlgItem(hDlg,IDC_STOP),FALSE);
			
			SetWindowLong(hDlg,DWL_USER,lParam);
			pDialog =  (SoundDialog*) lParam;
			return pDialog->OnInit(hDlg);

	} //switch(MSG)
	return 0;

}


////////////////////////////// WINDOWS MAIN ////////////////////////////////////////////////


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
 
//	STEP #1 
	
	WSADATA		ws_data;				// for microsoft socket 
	WORD		w_Version_Requested;	// for microsoft socket 
		
	SoundDialog			mySoundDialog;

	// initialize microsoft socket stuff..
	w_Version_Requested = MAKEWORD(1,1);
	WSAStartup(w_Version_Requested,&ws_data);


	DialogBoxParam(hInstance,MAKEINTRESOURCE(IDD_DIALOG1),NULL,DialogProc,
		(long)&mySoundDialog);

	WSACleanup();



	return 0;


}



