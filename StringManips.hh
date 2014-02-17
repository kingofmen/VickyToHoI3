#ifndef STRING_MANIPS_HH
#define STRING_MANIPS_HH

#include <string>
using namespace std; 

string addQuotes (string tag);
string remQuotes (string tag);
double days (string datestring);
string getField (string str, int field, char separator = ' ');
int year (string str); 
string convertMonth (string month);
string convertMonth (int month);

#endif
