/////////////////////////// header of wav file /////////////////////////////

typedef struct _Wav_Header {
     char				rID[4];            // 'RIFF' 
     long int			rLen;        
     char				wID[4];            // 'WAVE'        
     
     char				fId[4];            // 'fmt ' 
     long int			pcm_header_len;	   // varies...
	 
     short int			wFormatTag;
     short int			nChannels;			// 1,2 for stereo data is (l,r) pairs 
     long int			nSamplesPerSec;
     long int			nAvgBytesPerSec;
     short int			nBlockAlign;      
     short int			nBitsPerSample;

	 char dId[4];       // 'data' or 'fact' 
     long int dLen;

}Wav_Header;