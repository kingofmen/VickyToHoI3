#ifndef CLEANERWINDOW_HH
#define CLEANERWINDOW_HH

#include <QtGui>
#include <QObject>
#include <QThread> 
#include "Object.hh"
#include <map>
#include <fstream>

using namespace std;

class WorkerThread; 
class PopMerger; 

enum TaskType {LoadFile = 0,
	       Statistics,
	       AutoMap, 
	       Convert,
	       NumTasks}; 

class CleanerWindow : public QMainWindow {
  Q_OBJECT
  
public:
  CleanerWindow (QWidget* parent = 0); 
  ~CleanerWindow ();
  
  QPlainTextEdit* textWindow; 
  WorkerThread* worker;
  void openDebugLog (string fname);
  void closeDebugLog (); 
  void loadFile (string fname, TaskType autoTask = NumTasks);
						 
						 
						 
public slots:

  void loadFile (); 
  void getStats ();
  void autoMap (); 
  void convert ();
  void message (QString m); 
  
private:

};

struct ObjectSorter {
  ObjectSorter (string k) {keyword = k;} 
  string keyword;
};
struct ObjectAscendingSorter : public ObjectSorter {
public:
  ObjectAscendingSorter (string k) : ObjectSorter(k) {}
  bool operator() (Object* one, Object* two) {return (one->safeGetFloat(keyword) < two->safeGetFloat(keyword));} 
private:
};
struct ObjectDescendingSorter : public ObjectSorter {
public:
  ObjectDescendingSorter (string k) : ObjectSorter(k) {}
  bool operator() (Object* one, Object* two) {return (one->safeGetFloat(keyword) > two->safeGetFloat(keyword));} 
private:
};

double calcAvg (Object* ofthis); 

class WorkerThread : public QThread {
  Q_OBJECT
public:
  WorkerThread (string fname, TaskType aTask = NumTasks); 
  ~WorkerThread ();
  void setTask(TaskType t) {task = t;} 

protected:
  void run (); 
  
private:
  // Misc globals 
  string targetVersion;
  string sourceVersion; 
  string fname; 
  Object* vicGame;
  Object* hoiGame;
  TaskType task; 
  Object* configObject;
  TaskType autoTask;

  // Conversion processes
  bool convertBuildings (); 
  bool convertDiplomacy ();
  bool convertGovernments ();
  bool convertLaws (); 
  bool convertLeaders ();
  bool convertMisc (); 
  bool convertOoBs (); 
  bool convertProvinceOwners ();
  bool convertTechs ();
  bool listUrbanProvinces (); 
  bool moveCapitals ();
  bool moveIndustry ();
  bool moveResources ();
  bool moveStockpiles ();
  bool moveStrategicResources (); 
  
  // Infrastructure 
  void loadFile (string fname); 
  void getStatistics ();
  void autoMap (); 
  void convert ();   
  void configure ();

  // Initialisers
  bool createCountryMap ();
  bool createOutputDir ();
  bool createProvinceMap ();
  void initialiseHoiSummaries ();
  void initialiseVicSummaries ();   
  void loadFiles (); 
  void setupDebugLog ();
  
  // Helpers:
  void assignCountries (Object* vicCountry, Object* hoiCountry);
  void calcCasualties (Object* war);
  double calcCasualtiesInBattle (Object* battle, double decay);   
  double calcForceLimit (Object* navalBase); 
  double calculateGovResemblance (Object* vicCountry, Object* hoiCountry);
  double calculateVicProduction (Object* vicProvince, string resource, const objvec& goodClasses); 
  void cleanUp ();
  Object* createRegiment (int id, string type, string name, string keyword);
  string currentTags () const; 
  double extractStrength (Object* unit, Object* reserves);
  void getOfficers (objvec& candidates, string keyword, double total, unsigned int original);
  Object* loadTextFile (string fname);
  void makeHigher (objvec& lowHolder, int& numUnits, string name, string location, string keyword, objvec& highHolder);
  Object* selectHoiProvince (Object* vicProv);
  string selectHoiProvince (string vicLocation, Object* hoiCountry);
  void setPointersFromHoiCountry (Object* hc);
  void setPointersFromHoiTag (string tag);  
  void setPointersFromVicCountry (Object* vc);
  void setPointersFromVicProvince (Object* vp);
  void setPointersFromVicTag (string tag);
  bool swap (Object* one, Object* two, string key); 
  
  // Maps
  map<Object*, Object*> vicCountryToHoiCountryMap;
  map<Object*, Object*> hoiCountryToVicCountryMap;
  map<string, string> vicTagToHoiTagMap;
  map<string, string> hoiTagToVicTagMap;
  map<string, Object*> hoiTagToHoiCountryMap;
  map<string, Object*> vicProvIdToVicProvMap;
  map<string, Object*> hoiProvIdToHoiProvMap; 
  map<Object*, objvec> vicCountryToHoiProvsMap;
  map<Object*, objvec> hoiCountryToHoiProvsMap;
  map<Object*, objvec> vicProvToHoiProvsMap;
  map<Object*, objvec> hoiProvToVicProvsMap; 
  map<string, Object*> popIdMap; 
  map<string, int> hoiUnitTypes;
  map<string, int> vicUnitTypes;
  map<string, int> vicUnitsThatConvertToHoIUnits; 
  map<Object*, map<string, objvec> > vicCountryToUnitsMap; 
  map<string, int> vicTagToCoresMap;
  
  // Lists
  objvec vicProvinces;
  objvec vicCountries;  
  objvec hoiProvinces;
  objvec hoiCountries;
  objvec allHoiCountries; // Includes the ones with no provinces after conversion. 
  objvec hoiShipList; 
  
  // Input info
  Object* provinceMapObject;
  Object* countryMapObject;
  Object* provinceNamesObject; 
  Object* customObject;
  Object* vicTechObject;
  Object* leaderTypesObject; 
  map<string, Object*> hoiProvincePositions; 
}; 

#endif

