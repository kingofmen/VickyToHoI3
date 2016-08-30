#ifndef LOGGER_HH
#define LOGGER_HH

#include <QObject> 
#include <string> 
#include <map> 
#include "Parser.hh"

enum Debugs {DebugLeaders = 30,
	     DebugProvinces,
	     DebugMinisters,
	     DebugTechTeams,
	     DebugResources,
	     DebugTech,
	     DebugBuildings,
	     DebugUnits,
	     DebugStockpiles,
	     DebugSliders,
	     DebugIdeas,
	     DebugRevolters,
	     DebugCores, 
	     NumDebugs};

class Logger : public QObject {
  Q_OBJECT 
public: 
  Logger (); 
  ~Logger (); 
  enum DefaultLogs {Debug = 0, Trace, Game, Warning, Error}; 

  Logger& append (unsigned int prec, double val); 
  
  Logger& operator<< (std::string dat);
  Logger& operator<< (QString dat);
  Logger& operator<< (int dat);
  Logger& operator<< (double dat);
  Logger& operator<< (char dat);
  Logger& operator<< (char* dat);
  Logger& operator<< (const char* dat);
  Logger& operator<< (Object* dat); 
  void setActive (bool a) {active = a;}
  void setPrecision (int p = -1) {precision = p;}
  bool isActive () const {return active;} 
  
  static void createStream (int idx); 
  static Logger& logStream (int idx); 

signals:
  void message (QString m);
  
private:
  void clearBuffer (); 
  
  bool active;
  QString buffer; 
  int precision;
  
  static std::map<int, Logger*> logs; 
};


#endif
