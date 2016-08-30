#ifndef CLEANERWINDOW_HH
#define CLEANERWINDOW_HH

#include <QtGui>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QObject>
#include <QThread> 
#include "Object.hh"
#include <map>

using namespace std;

class WorkerThread; 
class PopMerger; 

class CleanerWindow : public QMainWindow {
  Q_OBJECT
  
public:
  CleanerWindow (QWidget* parent = 0); 
  ~CleanerWindow ();
  
  QPlainTextEdit* textWindow; 
  WorkerThread* worker;
  
  void loadFile (string fname, int autoTask = -1);
						      
public slots:

  void loadFile (); 
  void cleanFile ();
  void getStats ();
  void convert ();
  void findTeams ();
  void colourMap (); 
  void message (QString m); 
  void mergePops (); 
  
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

struct VoterInfo {
  void addPop (Object* pop, Object* voterConfig);
  double getIssuePercentage (int issue) {if (issue > (int) issues.size()) return 0; if (0 > issue) return 0; return issues[issue]/weightedTotal;}
  double getIdeologyPercentage (int ideology) {if (ideology > (int) ideologies.size()) return 0; if (0 > ideology) return 0; return ideologies[ideology]/weightedTotal;}
  double calculateDiversity () const;
  double getDissent () const {return dissent / total;} 
  vector<double> issues;
  vector<double> ideologies;
  map<string, double> cultures;
  map<string, double> religion; 
  double total;
  double weightedTotal;
  double dissent; 
};

struct ClassInfo {
  // POP type to total weighted and unweighted, then accepted culture. 
  map<string, pair<pair<double, double>, pair<double, double> > > classNumbers;
  void addTotal           (string c, double n) {classNumbers[c].first.first += n; classNumbers["total"].first.first += n; total += n;}
  void addWeightedTotal   (string c, double n) {classNumbers[c].first.second += n; classNumbers["total"].first.second += n;}
  void addCulture         (string c, double n) {classNumbers[c].second.first += n; classNumbers["total"].second.first += n;}
  void addWeightedCulture (string c, double n) {classNumbers[c].second.second += n; classNumbers["total"].second.second += n;}

  double getTotal           (string c) {return classNumbers[c].first.first;}
  double getWeightedTotal   (string c) {return classNumbers[c].first.second;}
  double getCulture         (string c) {return classNumbers[c].second.first;}
  double getWeightedCulture (string c) {return classNumbers[c].second.second;}

  double getPercentage (string c) {return getTotal(c) / total;}
  
private:
  double total; 
};

struct MapInfo {
  double vicWidth;
  double vicHeight;
  double hoiWidth;
  double hoiHeight;

  double distanceHoiToHoi (Object* hpi1, Object* hpi2);
  double distanceHoiToVic (Object* hpi, Object* vpi); 
};

class WorkerThread : public QThread {
  Q_OBJECT
public:
  WorkerThread (string fname, int aTask = -1); 
  ~WorkerThread ();

  enum TaskType {LoadFile = 0, CleanFile, Statistics, Convert, MergePops, FindTeams, ColourMap, NumTasks}; 
  void setTask(TaskType t) {task = t;} 

protected:
  void run (); 
  
private:
  // Misc globals 
  string targetVersion;
  string sourceVersion; 
  string fname; 
  Object* hoiGame;
  Object* vicGame;
  TaskType task; 
  Object* configObject;
  Object* customObject; 
  int autoTask;
  MapInfo* mapInfo; 
  PopMerger* popMerger; 
  
  // Infrastructure 
  void loadFile (string fname); 
  void cleanFile ();
  void getStatistics ();
  void convert (); 
  void configure ();
  void mergePops ();
  void findTeams ();
  void colourHoiMap (); 

  // Helper methods
  void addCultureCores(); 
  void addProvinceToHoiNation (Object* hoiProv, Object* hoiNation);    
  void addProvinceToVicNation (Object* hoiProv, Object* vicNation);
  void assignCountries (Object* hoi, Object* vic);
  void calculateCountryQualities ();
  void calculateGovTypes ();
  void calculateProvinceWeight (Object* vic, const vector<string>& goods, const vector<string>& bonusTypes, Object* weightObject, map<string, double>& totalWeights); 
  void checkForExtra (Object* extraUnits, map<string, int>& totalVicRegiments, map<string, int>& existing); 
  void cleanUp ();
  void countPops ();
  void createKeyList (Object* provNames); 
  void createUtilityMaps ();
  double days (string datestring);
  void fillVicVectors (); 
  void findBestIdea (objvec& ideas, Object* vicCountry, vector<string>& qualia); 
  Object* findHoiCountryByHoiTag (string tag);
  Object* findVicCountryByVicTag (string tag);
  Object* findVicCountryByHoiCountry (Object* hoi);
  Object* findHoiCountryByVicCountry (Object* vic);
  string findVicTagFromHoiTag (string hoitag, bool quotes = false);
  Object* findHoiCapitalFromVicCountry (Object* vic);
  Object* findHoiProvInfoFromHoiProvince (Object* hoi);
  Object* findHoiProvinceFromHoiId (string id);
  Object* findHoiProvInfoFromHoiId (string id);  
  Object* findHoiProvinceFromHoiProvInfo (Object* hpi);
  Object* findNavyLocation(Object* vicProv, vector<pair<Object*, string> >& fail, Object* hoiCountry);
  void generateEvents (Object* evtObject, Object* vicCountry); 
  int issueToNumber (string issue) const; 
  Object* loadTextFile (string fname);
  void mergeBuilding (Object* building, objvec& provs, string keyword);   
  void moveAnyLeaders (objvec& officerRanks, string keyword, int index, vector<objvec>& theOfficers);
  void prepareCountries ();
  void recalculateProvinceMap ();
  void recalculateProvinceMapNew ();
  void recalculateProvinceMapTriangulate ();
  bool recursiveBuy (string techid, Object* hoiCountry, double& points, map<string, bool>& gotTechs, Object* vicCountry); 
  void setAcceptedStatus ();  
  void setKeys (Object* hpi, Object* vicKey); 
  
  // Inputs 
  void createProvinceMappings ();
  void createCountryMappings (); 

  // Conversion processes
  void calculateKustomPoints ();
  void desperationMoveCapitals (); 
  void diplomacy ();
  void dissent (); 
  void fixHeader ();
  void fixGlobals ();
  void ideas ();
  void mobEvents (); 
  void moveCapitals ();
  void moveCores ();
  void moveControls ();    
  void moveProvinces ();
  void moveResources (); 
  void moveTechTeams ();
  void moveLeaders ();
  void moveMinisters ();
  void provinceStructures ();
  void revolters (); 
  void sliders ();
  void stockpiles (); 
  void techs ();
  void units (); 
  
  // Storage and maps
  map<string, Object*> vicTagToVicCountryMap;
  map<string, Object*> hoiTagToHoiCountryMap;
  map<Object*, objvec> hoiProvinceToVicProvincesMap;
  map<Object*, objvec> vicProvinceToHoiProvincesMap;
  map<Object*, Object*> vicProvinceToVicStateMap;
  map<Object*, ClassInfo*> vicCountryToClassInfoMap;
  map<Object*, VoterInfo*> vicCountryToVoterInfoMap;   
  
  objvec hoiProvInfos; 
  objvec hoiProvinces;
  objvec hoiCountries;
  objvec vicProvinces;
  objvec vicCountries;
  objvec techTeams;
  objvec vicStates; 
  map<string, map<string, objvec> > ministers; // Category -> position -> ministers
  map<string, map<string, int> > personalities; // Position -> personality -> number 
  objvec generalRanks;
  objvec admiralRanks;
  objvec commderRanks;  
  vector<objvec> generals;
  vector<objvec> admirals;
  vector<objvec> commders;
  objvec vicParties;
  map<string, int> landdivs;
  map<string, int> navaldivs;
  map<string, int> airdivs;  
  map<string, int> landbrigs;
  map<string, int> navalbrigs;
  map<string, int> airbrigs;
  map<Object*, map<Object*, int> > hoiCountriesRelations;
  map<string, map<string, bool> > brigadeAcceptable;
  map<string, int> tagToSizeMap;
  map<string, Object*> idToPopMap;
  vector<string> hoiProducts;
  map<Object*, map<string, double> > incomeMap;
  map<string, map<string, pair<double, double> > > unitTypeToMaxSuppliesMap;
  map<Object*, Object*> hoiRegionsMap; 
}; 

#endif

