


class Time
{
public:
	unsigned short hours;
	unsigned short minutes;
	unsigned short seconds;
	unsigned short milliseconds;

	Time(unsigned short h, unsigned short m, unsigned short s,unsigned short mi);
	~Time(void);
	void NormalizeTime(void);
	void Display(void);
	Time *operator-(Time aTime);
	Time *operator+(Time aTime);
	void operator*=(unsigned short num);
};

Time::Time(unsigned short h, unsigned short m, unsigned short s,unsigned short mi)
{
		
	milliseconds = mi;
	seconds = s;
	minutes = m;
	hours = h;

	NormalizeTime();
}

Time::~Time(void)
{

}
void Time::NormalizeTime(void)
{ 	
	hours   = hours +  ( (minutes + (seconds / 60) + (milliseconds / 3600000) ) / 60);
	minutes =    ( (minutes + (seconds/60) + (milliseconds / 60000 ) ) % 60);
	seconds = ( (seconds + (milliseconds / 1000)) % 60);
	milliseconds %= 1000;
	// was : seconds =  seconds  = seconds % 60

}

void Time::Display(void)
{
	//cout << "(" << hours << ":" << minutes << ":" << seconds << ":" << milliseconds << ")\n";
}

Time *Time::operator-(Time aTime)
{
	unsigned short  h;
	unsigned short m;
	unsigned short s;
	unsigned short mil;
	Time *tempTimePtr;

	h = hours - aTime.hours;
	m = minutes - aTime.minutes;
	s = seconds -  aTime.seconds;
	mil = milliseconds - aTime.milliseconds;

	tempTimePtr = new Time(h,m,s,mil);

	return tempTimePtr;
}

Time *Time::operator+(Time aTime)
{
	unsigned short  h;
	unsigned short m;
	unsigned short s;
	unsigned short mil;
	Time *tempTimePtr;

	h = hours + aTime.hours;
	m = minutes + aTime.minutes;
	s = seconds +  aTime.seconds;
	mil = milliseconds +  aTime.milliseconds;

	tempTimePtr = new Time(h,m,s,mil);

	return tempTimePtr;
}


void Time::operator *=(unsigned short num)
{
	hours		   *=	num;
	minutes		   *=	num;
	seconds		   *=	num;
	milliseconds   *=	num;

	NormalizeTime();
}
