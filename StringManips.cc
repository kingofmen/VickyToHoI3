#include "StringManips.hh"
#include "boost/tokenizer.hpp"

string addQuotes (string tag) {
  string ret("\"");
  ret += tag;
  ret += "\"";
  return ret; 
}

string remQuotes (string tag) {
  int firstQuote = -1;
  for (unsigned int i = 0; i < tag.size(); ++i) {
    char curr = tag[i];
    if (curr != '"') continue;
    firstQuote = i;
    break;
  }

  if (-1 == firstQuote) return tag;
    
  string ret;
  for (unsigned int i = firstQuote+1; i < tag.size(); ++i) {
    char curr = tag[i];
    if (curr == '"') break;
    ret += curr; 
  }
  return ret; 
}

string getField (string str, int field, char separator) {
  size_t start = 0;
  size_t stops = str.find(separator); 
  for (int i = 0; i < field; ++i) {
    start = stops+1;
    stops = str.find(separator, start); 
  }
  return str.substr(start, stops); 
}

int year (string str) {
  return atoi(getField(remQuotes(str), 0, '.').c_str()); 
}

double days (string datestring) {
  boost::char_separator<char> sep(".");
  boost::tokenizer<boost::char_separator<char> > tokens(datestring, sep);
  boost::tokenizer<boost::char_separator<char> >::iterator i = tokens.begin();
  int year = atoi((*i).c_str()); ++i;
  if (i == tokens.end()) {
    return -1;
  }
  int month = atoi((*i).c_str()); ++i;
  if (i == tokens.end()) {
    return -1;
  }
  int day = atoi((*i).c_str());

  double ret = day;
  ret += year*365;
  switch (month) { // Cannot add Dec, it'll be previous year
  case 12: ret += 30; // Nov
  case 11: ret += 31; // Oct
  case 10: ret += 30; // Sep
  case 9:  ret += 31; // Aug
  case 8:  ret += 31; // Jul
  case 7:  ret += 30; // Jun
  case 6:  ret += 31; // May
  case 5:  ret += 30; // Apr
  case 4:  ret += 31; // Mar
  case 3:  ret += 28; // Feb
  case 2:  ret += 31; // Jan 
  case 1:  // Nothing to add to previous year 
  default: break; 
  }
  return ret; 
}

string convertMonth (string month) {
  if (month == "1")       return "january";
  else if (month == "2")  return "february";
  else if (month == "3")  return "march";
  else if (month == "4")  return "april";
  else if (month == "5")  return "may";
  else if (month == "6")  return "june";
  else if (month == "7")  return "july";
  else if (month == "8")  return "august";
  else if (month == "9")  return "september";
  else if (month == "10") return "october";
  else if (month == "11") return "november";
  else if (month == "12") return "december";
  else return "january"; 
}

string convertMonth (int month) {
  if (month == 1)       return "january";
  else if (month == 2)  return "february";
  else if (month == 3)  return "march";
  else if (month == 4)  return "april";
  else if (month == 5)  return "may";
  else if (month == 6)  return "june";
  else if (month == 7)  return "july";
  else if (month == 8)  return "august";
  else if (month == 9)  return "september";
  else if (month == 10) return "october";
  else if (month == 11) return "november";
  else if (month == 12) return "december";
  else return "january"; 
}
