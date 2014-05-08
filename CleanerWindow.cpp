#include "CleanerWindow.hh" 
#include <QPainter> 
#include "Parser.hh"
#include <cstdio> 
#include <QtGui>
#include <QDesktopWidget>
#include <QRect>
#include <iostream> 
#include <string>
#include "Logger.hh" 
#include <set>
#include <algorithm>
#include "StructUtils.hh" 
#include "StringManips.hh"
#include <direct.h>

using namespace std; 

/* TODO:

*/

/* Nice to have:
   - Debt? 
   - Tweak some province conversions - Sitka is inland? Lots of Vic provinces with naval bases don't find a coastal HoI province.
   - Also several Vic provinces with airbases don't find anywhere to put them. 
 */

char stringbuffer[10000];
CleanerWindow* parentWindow;
ofstream* debugFile = 0; 

Object* hoiCountry = 0;
Object* vicCountry = 0;
string hoiTag;
string vicTag; 

const string NO_OWNER = "\"NONE\""; 
const string HOI_FINEST_HOUR = ".\\HoI_TFH\\";
const string HOI_VANILLA = ".\\HoI_Vanilla\\";
const string NFCON = "NOT_FOUND_IN_CONFIG";

int main (int argc, char** argv) {
  QApplication industryApp(argc, argv);
  QDesktopWidget* desk = QApplication::desktop();
  QRect scr = desk->availableGeometry();
  parentWindow = new CleanerWindow();
  parentWindow->show();
  srand(42); 
  
  parentWindow->resize(3*scr.width()/5, scr.height()/2);
  parentWindow->move(scr.width()/5, scr.height()/4);
  parentWindow->setWindowTitle(QApplication::translate("toplevel", "Vic2 to HoI3 converter"));
 
  QMenuBar* menuBar = parentWindow->menuBar();
  QMenu* fileMenu = menuBar->addMenu("File");
  QAction* newGame = fileMenu->addAction("Load file");
  QAction* automap = fileMenu->addAction("Province links");  
  QAction* quit    = fileMenu->addAction("Quit");
  QObject::connect(automap, SIGNAL(triggered()), parentWindow, SLOT(autoMap()));   
  QObject::connect(quit, SIGNAL(triggered()), parentWindow, SLOT(close())); 
  QObject::connect(newGame, SIGNAL(triggered()), parentWindow, SLOT(loadFile())); 

  QMenu* actionMenu = menuBar->addMenu("Actions");
  QAction* convert = actionMenu->addAction("Convert to HoI");
  QObject::connect(convert, SIGNAL(triggered()), parentWindow, SLOT(convert()));
  QAction* stats = actionMenu->addAction("Stats");
  QObject::connect(stats, SIGNAL(triggered()), parentWindow, SLOT(getStats())); 
  
  parentWindow->textWindow = new QPlainTextEdit(parentWindow);
  parentWindow->textWindow->setFixedSize(3*scr.width()/5 - 10, scr.height()/2-40);
  parentWindow->textWindow->move(5, 30);
  parentWindow->textWindow->show(); 

  Logger::createStream(Logger::Debug);
  Logger::createStream(Logger::Trace);
  Logger::createStream(Logger::Game);
  Logger::createStream(Logger::Warning);
  Logger::createStream(Logger::Error);

  QObject::connect(&(Logger::logStream(Logger::Debug)),   SIGNAL(message(QString)), parentWindow, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Trace)),   SIGNAL(message(QString)), parentWindow, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Game)),    SIGNAL(message(QString)), parentWindow, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Warning)), SIGNAL(message(QString)), parentWindow, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Error)),   SIGNAL(message(QString)), parentWindow, SLOT(message(QString)));

  for (int i = DebugLeaders; i < NumDebugs; ++i) {
    Logger::createStream(i);
    QObject::connect(&(Logger::logStream(i)), SIGNAL(message(QString)), parentWindow, SLOT(message(QString)));
    Logger::logStream(i).setActive(false); 
  } 

  parentWindow->show();
  if (argc > 1) {
    if (atoi(argv[1]) == AutoMap) parentWindow->autoMap(); 
    else parentWindow->loadFile(argv[2], (TaskType) atoi(argv[1])); 
  }
  int ret = industryApp.exec();
  parentWindow->closeDebugLog();   
  return ret; 
}


CleanerWindow::CleanerWindow (QWidget* parent) 
  : QMainWindow(parent)
  , worker(0)
{}

CleanerWindow::~CleanerWindow () {}

void CleanerWindow::message (QString m) {
  textWindow->appendPlainText(m);
  if (debugFile) (*debugFile) << m.toAscii().data() << std::endl;      
}

void CleanerWindow::loadFile () {
  QString filename = QFileDialog::getOpenFileName(this, tr("Select file"), QString(""), QString("*.ck2"));
  string fn(filename.toAscii().data());
  if (fn == "") return;
  loadFile(fn);   
}

void CleanerWindow::loadFile (string fname, TaskType autoTask) {
  if (worker) delete worker;
  worker = new WorkerThread(fname, autoTask);
  worker->start();
}

void CleanerWindow::getStats () {
  Logger::logStream(Logger::Game) << "Starting statistics.\n";
  worker->setTask(Statistics); 
  worker->start(); 
}

void CleanerWindow::convert () {
  Logger::logStream(Logger::Game) << "Convert to HoI.\n";
  worker->setTask(Convert); 
  worker->start(); 
}

void CleanerWindow::autoMap () {
  worker = new WorkerThread("", AutoMap);
  Logger::logStream(Logger::Game) << "Generate province mapping. NB! This does not give good results due to differing projections.\n";  
  worker->start(); 
}

void CleanerWindow::closeDebugLog () {
  if (!debugFile) return;
  debugFile->close();
  delete debugFile;
  debugFile = 0; 
}

void CleanerWindow::openDebugLog (string fname) {
  if (fname == "") return;
  if (debugFile) closeDebugLog();
  debugFile = new ofstream(fname.c_str(), ios_base::trunc);
}

WorkerThread::WorkerThread (string fn, TaskType atask)
  : targetVersion(HOI_FINEST_HOUR)
  , sourceVersion(".\\V2_HoD\\")
  , fname(fn)
  , vicGame(0)
  , hoiGame(0)
  , task(LoadFile)
  , configObject(0)
  , autoTask(atask)
  , provinceMapObject(0)
  , countryMapObject(0)
  , provinceNamesObject(0)
  , customObject(0)
{
  configure(); 
  if (!createOutputDir()) {
    Logger::logStream(Logger::Error) << "Error: No output directory, could not create one. Fix this before proceeding.\n";
    autoTask = NumTasks; 
  }  
}  

WorkerThread::~WorkerThread () {
  if (hoiGame) delete hoiGame;
  if (vicGame) delete vicGame; 
  hoiGame = 0;
  vicGame = 0; 
}

void WorkerThread::run () {
  setupDebugLog();  
  if (AutoMap == autoTask) task = AutoMap; 
  
  switch (task) {
  case LoadFile: loadFile(fname); break;
  case Statistics: getStatistics(); break;
  case Convert: convert(); break;
  case AutoMap: autoMap(); break;        
  case NumTasks: 
  default: break; 
  }
}

void WorkerThread::loadFile (string fname) {
  vicGame = loadTextFile(fname);
  if (NumTasks != autoTask) {
    task = autoTask; 
    switch (autoTask) {
    case Convert:
      convert(); 
      break;
    case Statistics:
      getStatistics();
      break;
    default:
      break;
    }
  }
}

void WorkerThread::assignCountries (Object* vicCountry, Object* hoiCountry) {
  vicTag = vicCountry->getKey();
  hoiTag = hoiCountry->getKey();
  vicCountryToHoiCountryMap[vicCountry] = hoiCountry;
  hoiCountryToVicCountryMap[hoiCountry] = vicCountry;
  vicTagToHoiTagMap[vicTag] = hoiTag;
  hoiTagToVicTagMap[hoiTag] = vicTag;
  if (find(hoiCountries.begin(), hoiCountries.end(), hoiCountry) == hoiCountries.end()) hoiCountries.push_back(hoiCountry);
  if (find(vicCountries.begin(), vicCountries.end(), vicCountry) == vicCountries.end()) vicCountries.push_back(vicCountry);
  hoiTagToHoiCountryMap[hoiTag] = hoiCountry; 
  Logger::logStream(DebugCountries) << "Assigning Vic country " << vicTag << " <-> HoI " << hoiTag << "\n"; 
}

void WorkerThread::cleanUp () {
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    (*hp)->unsetValue("name");
    (*hp)->unsetValue("infraSet"); 
    (*hp)->unsetValue("vicIndustry");
    (*hp)->unsetValue("vicUnemployed");
    (*hp)->unsetValue("coastal"); 
  }

  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    (*hc)->unsetValue("convoy"); 
    (*hc)->unsetValue("military_construction"); 
    (*hc)->unsetValue("cap_pool"); 
    (*hc)->resetLeaf("nukes", "0.000"); 
    
    Object* spy_mission    = (*hc)->getNeededObject("spy_mission");
    Object* spy_date       = (*hc)->getNeededObject("spy_date");
    Object* spy_allocation = (*hc)->getNeededObject("spy_allocation");
    Object* spy_priority   = (*hc)->getNeededObject("spy_priority");
    
    spy_mission->clear();
    spy_date->clear();
    spy_allocation->clear();
    spy_priority->clear();
    for (unsigned int i = 0; i < hoiCountries.size(); ++i) {
      spy_mission->addToList("0"); 
      spy_date->addToList("1.1.1.0");
      spy_allocation->addToList("0.000");
      spy_priority->addToList("0"); 
    }

    (*hc)->unsetValue("ai"); 
  }
}

void WorkerThread::configure () {
  configObject = processFile("config.txt");
  targetVersion = configObject->safeGetString("hoidir", HOI_FINEST_HOUR);
  sourceVersion = configObject->safeGetString("vicdir", ".\\V2_HoD\\");
  Logger::logStream(Logger::Debug).setActive(false);

  Object* debug = configObject->safeGetObject("debug");
  if (debug) {
    if (debug->safeGetString("generic", "no") == "yes") Logger::logStream(Logger::Debug).setActive(true);
    bool activateAll = (debug->safeGetString("all", "no") == "yes");
    for (int i = DebugLeaders; i < NumDebugs; ++i) {
      sprintf(stringbuffer, "%i", i);
      if ((activateAll) || (debug->safeGetString(stringbuffer, "no") == "yes")) Logger::logStream(i).setActive(true);
    }
  }

  Object* hoiPositions = loadTextFile(targetVersion + "positions.txt");
  objvec posvec = hoiPositions->getLeaves();
  for (objiter pos = posvec.begin(); pos != posvec.end(); ++pos) {
    hoiProvincePositions[(*pos)->getKey()] = (*pos); 
  }

  vicTechObject = loadTextFile(sourceVersion + "technologies.txt");
  leaderTypesObject = loadTextFile(targetVersion + "leaderTypes.txt"); 
}

Object* WorkerThread::loadTextFile (string fname) {
  Logger::logStream(Logger::Game) << "Parsing file " << fname << "\n";
  ifstream reader;
  reader.open(fname.c_str());
  if ((reader.eof()) || (reader.fail())) {
    Logger::logStream(Logger::Error) << "Could not open file, returning null object.\n";
    return 0; 
  }
  
  Object* ret = processFile(fname);
  Logger::logStream(Logger::Game) << " ... done.\n";
  return ret; 
}

string nameAndNumber (Object* eu3prov) {
  return eu3prov->getKey() + " (" + remQuotes(eu3prov->safeGetString("name", "\"could not find name\"")) + ")";
}

Object* WorkerThread::selectHoiProvince (Object* vicProv) {
  Object* hoiProv = 0;
  for (objiter hp = vicProvToHoiProvsMap[vicProv].begin(); hp != vicProvToHoiProvsMap[vicProv].end(); ++hp) {
    if (remQuotes((*hp)->safeGetString("owner")) != hoiTag) continue;
    hoiProv = (*hp);
    if ((*hp)->safeGetString("name") == vicProv->safeGetString("name")) break;
  }
  return hoiProv; 
}

void WorkerThread::setPointersFromHoiCountry (Object* hc) {
  hoiCountry = hc;
  vicCountry = hoiCountryToVicCountryMap[hoiCountry];
  hoiTag = hoiCountry->getKey();
  if (vicCountry) vicTag = vicCountry->getKey();
  else vicTag = "NONE"; 
}

void WorkerThread::setPointersFromVicCountry (Object* vc) {
  vicCountry = vc;
  hoiCountry = vicCountryToHoiCountryMap[vicCountry];
  if (hoiCountry) hoiTag = hoiCountry->getKey();
  else hoiTag = "NONE"; 
  vicTag = vicCountry->getKey();
}

void WorkerThread::setPointersFromVicProvince (Object* vp) {
  vicTag = remQuotes(vp->safeGetString("owner", NO_OWNER));
  hoiTag = vicTagToHoiTagMap[vicTag];
  hoiCountry = hoiTagToHoiCountryMap[hoiTag];
  vicCountry = hoiCountryToVicCountryMap[hoiCountry];
}

void WorkerThread::setPointersFromVicTag (string tag) {
  vicTag = tag;
  hoiTag = vicTagToHoiTagMap[vicTag];
  hoiCountry = hoiTagToHoiCountryMap[hoiTag];
  vicCountry = hoiCountryToVicCountryMap[hoiCountry];
}

void WorkerThread::setPointersFromHoiTag (string tag) {
  hoiTag = tag;
  vicTag = hoiTagToVicTagMap[hoiTag];
  hoiCountry = hoiTagToHoiCountryMap[hoiTag];
  vicCountry = hoiCountryToVicCountryMap[hoiCountry];
}

bool WorkerThread::swap (Object* one, Object* two, string key) {
  if (one == two) return true; 
  Object* valOne = one->safeGetObject(key);
  if (!valOne) return false;
  Object* valTwo = two->safeGetObject(key);
  if (!valTwo) return false;

  one->unsetValue(key);
  two->unsetValue(key);
  one->setValue(valTwo);
  two->setValue(valOne);
  return true; 
}


/********************************  End helpers  **********************/

/******************************** Begin initialisers **********************/

bool WorkerThread::createCountryMap () {
  if (!countryMapObject) {
    Logger::logStream(Logger::Error) << "Error: Could not find country-mapping object.\n";
    return false; 
  }

  map<string, int> vicTagToProvsMap;
  map<string, int> vicTagToCoresMap;

  if (customObject) {
    Object* countryCustom = customObject->safeGetObject("countries");
    objvec overrides = countryCustom->getLeaves();
    for (objiter over = overrides.begin(); over != overrides.end(); ++over) {
      vicTag = (*over)->getKey();
      vicCountry = vicGame->safeGetObject(vicTag);
      if (vicCountryToHoiCountryMap[vicCountry]) {
	Logger::logStream(Logger::Game) << "Skipping custom assignment of Vic tag " << vicTag
					<< " as it is already assigned to " << vicCountryToHoiCountryMap[vicCountry]->getKey()
					<< ".\n";
	continue;
      }
      
      hoiTag = (*over)->getLeaf();      
      hoiCountry = hoiGame->safeGetObject(hoiTag);
      if (hoiCountryToVicCountryMap[hoiCountry]) {
	Logger::logStream(Logger::Game) << "Skipping custom assignment of Vic tag " << vicTag
					<< " as " << hoiTag << " is already assigned to " << hoiCountryToVicCountryMap[hoiCountry]->getKey()
					<< ".\n";
	continue;
      }

      assignCountries(vicCountry, hoiCountry); 
    }
  }
  
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    vicTagToProvsMap[remQuotes((*vp)->safeGetString("owner", "NONE"))]++;
    vicTagToProvsMap[remQuotes((*vp)->safeGetString("controller", "NONE"))]++;
    objvec cores = (*vp)->getValue("core");
    for (objiter c = cores.begin(); c != cores.end(); ++c) {
      vicTagToCoresMap[remQuotes((*c)->getLeaf())]++; 
    }
  }
  
  objvec links = countryMapObject->getValue("link");
  for (objiter link = links.begin(); link != links.end(); ++link) {
    objvec vics = (*link)->getValue("vic");
    vicCountry = 0;

    for (objiter v = vics.begin(); v != vics.end(); ++v) {
      Object* vCurr = vicGame->safeGetObject((*v)->getLeaf());
      if (!vCurr) continue;
      if (vicCountryToHoiCountryMap[vCurr]) continue;
      vicCountry = vCurr;
      break;
    }
    if (!vicCountry) continue;
    if (0 == vicTagToProvsMap[vicCountry->getKey()]) continue; 

    objvec hois = (*link)->getValue("hoi");
    hoiCountry = 0;    
    for (objiter h = hois.begin(); h != hois.end(); ++h) {
      Object* hCurr = hoiGame->safeGetObject((*h)->getLeaf());
      if (!hCurr) continue;
      if (hoiCountryToVicCountryMap[hCurr]) continue;
      hoiCountry = hCurr;
      break;
    }
    if (!hoiCountry) {
      continue;
    }

    assignCountries(vicCountry, hoiCountry); 
  }

  objvec leaves = vicGame->getLeaves();
  objvec vicUnassigned;
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    if (!(*leaf)->safeGetObject("technology")) continue;
    if (vicCountryToHoiCountryMap[*leaf]) continue;
    vicUnassigned.push_back(*leaf);
  }

  leaves = hoiGame->getLeaves();
  objvec hoiUnassigned;
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    if (!(*leaf)->safeGetObject("technology")) continue;
    if (hoiCountryToVicCountryMap[*leaf]) continue;
    if ((*leaf)->getKey() == "REB") continue;
    hoiUnassigned.push_back(*leaf);
  }

  objvec vicRevolters;
  while (0 < vicUnassigned.size()) {
    vicCountry = vicUnassigned.back();
    vicUnassigned.pop_back();
    if (0 == vicTagToProvsMap[vicCountry->getKey()]) {
      vicRevolters.push_back(vicCountry); 
      continue;
    }
    if (0 == hoiUnassigned.size()) {
      Logger::logStream(Logger::Warning) << "Warning: Unable to assign Vic country " << vicCountry->getKey() << ".\n";
      continue; 
    }
    hoiCountry = hoiUnassigned.back();
    hoiUnassigned.pop_back();
    assignCountries(vicCountry, hoiCountry); 
  }

  while (0 < vicRevolters.size()) {
    if (0 == hoiUnassigned.size()) break;    
    vicCountry = vicRevolters.back();
    vicRevolters.pop_back();
    if (0 == vicTagToCoresMap[vicCountry->getKey()]) continue; 
    hoiCountry = hoiUnassigned.back();
    hoiUnassigned.pop_back();
    assignCountries(vicCountry, hoiCountry); 
  }
  
  return true; 
}

bool WorkerThread::createOutputDir () {
  DWORD attribs = GetFileAttributesA("Output");
  if (attribs == INVALID_FILE_ATTRIBUTES) {
    Logger::logStream(Logger::Warning) << "Warning, no Output directory, attempting to create one.\n";
    int error = _mkdir("Output");
    if (-1 == error) {
      Logger::logStream(Logger::Error) << "Error: Could not create Output directory. Aborting.\n";
      return false; 
    }
  }
  return true; 
}

bool WorkerThread::createProvinceMap () {
  if (!provinceMapObject) {
    Logger::logStream(Logger::Error) << "Error: Could not find province-mapping object.\n";
    return false; 
  }

  objvec links = provinceMapObject->getValue("link");
  for (objiter link = links.begin(); link != links.end(); ++link) {
    objvec vicProvIds = (*link)->getValue("vic");
    objvec hoiProvIds = (*link)->getValue("hoi");
    for (objiter hoi = hoiProvIds.begin(); hoi != hoiProvIds.end(); ++hoi) {
      string hoiProvId = (*hoi)->getLeaf(); 
      Object* hoiProv = hoiGame->safeGetObject(hoiProvId);
      if (!hoiProv) {
	Logger::logStream(Logger::Warning) << "Warning: Could not find alleged HoI province " << hoiProvId << ", skipping.\n";
	continue;
      }
      for (objiter vic = vicProvIds.begin(); vic != vicProvIds.end(); ++vic) {
	string vicProvId = (*vic)->getLeaf(); 
	Object* vicProv = vicGame->safeGetObject(vicProvId);
	
	if (!vicProv) {
	  Logger::logStream(Logger::Warning) << "Warning: Could not find alleged Vicky province " << vicProvId << ", skipping.\n";
	  continue;
	}
      
	hoiProvToVicProvsMap[hoiProv].push_back(vicProv);
	vicProvToHoiProvsMap[vicProv].push_back(hoiProv);
      }
    }
  }

  objvec leaves = vicGame->getLeaves();
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    if ((*leaf)->safeGetString("owner", "NONE") == "NONE") continue;
    vicProvinces.push_back(*leaf);
    vicProvIdToVicProvMap[(*leaf)->getKey()] = (*leaf); 
    if (0 < vicProvToHoiProvsMap[*leaf].size()) continue;
    Logger::logStream(Logger::Warning) << "Warning: Vicky province " << nameAndNumber(*leaf) << " has no assigned HoI provinces.\n"; 
  }

  leaves = hoiGame->getLeaves();
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    string ownertag = (*leaf)->safeGetString("owner", "NONE");
    if (ownertag == "NONE") continue;
    hoiProvinces.push_back(*leaf);
    hoiProvIdToHoiProvMap[(*leaf)->getKey()] = (*leaf); 
    (*leaf)->resetLeaf("name", provinceNamesObject->safeGetString((*leaf)->getKey(), "NO_NAME"));
    (*leaf)->unsetValue("capital");
    Object* pool = (*leaf)->safeGetObject("pool");
    if (pool) {
      Object* owner = hoiGame->safeGetObject(remQuotes(ownertag));
      if (owner) {
	owner = owner->getNeededObject("cap_pool");
	objvec resources = pool->getLeaves();
	for (objiter resource = resources.begin(); resource != resources.end(); ++resource) {
	  string key = (*resource)->getKey();
	  owner->resetLeaf(key, pool->safeGetFloat(key) + owner->safeGetFloat(key)); 
	}
      }
      (*leaf)->unsetValue("pool");
    }
    if (0 < hoiProvToVicProvsMap[*leaf].size()) continue; 
    Logger::logStream(Logger::Warning) << "Warning: Hoi province " << (*leaf)->getKey() << " has no assigned Vicky province.\n"; 
  }

  return true; 
}

void WorkerThread::initialiseHoiSummaries () {
  objvec leaves = hoiGame->getLeaves();
  Object* hoiShipWeights = configObject->getNeededObject("hoiShips"); 
  static map<string, bool> printed; 

  Object* reserveObject = new Object("reserveFractions");
  configObject->setValue(reserveObject);
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    if (!(*leaf)->safeGetObject("flags")) continue;
    if (targetVersion == HOI_FINEST_HOUR) {
      if (!(*leaf)->safeGetObject("strategic_warfare")) continue;
    }
    allHoiCountries.push_back(*leaf);
    objvec armies = (*leaf)->getValue("theatre");
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      extractStrength(*army, reserveObject);
      objvec navies = (*army)->getValue("navy");
      for (objiter navy = navies.begin(); navy != navies.end(); ++navy) {
	objvec ships = (*navy)->getValue("ship");
	for (objiter ship = ships.begin(); ship != ships.end(); ++ship) {
	  double weight = -1;
	  string shiptype = remQuotes((*ship)->safeGetString("type"));
	  weight = hoiShipWeights->safeGetFloat(shiptype, -1);
	  if (-1 == weight) {
	    weight = 1;
	    if (!printed[shiptype]) {
	      Logger::logStream(Logger::Warning) << "Warning: Unknown HoI ship type " << shiptype << ", assigning weight 1.\n";
	      printed[shiptype] = true;
	    }
	  }
	  hoiShipList.push_back(*ship); 
	}
      }
      objvec airs = (*army)->getValue("air");
      for (objiter air = airs.begin(); air != airs.end(); ++air) {
	objvec wings = (*air)->getValue("wing");
	for (objiter wing = wings.begin(); wing != wings.end(); ++wing) {
	  hoiUnitTypes[remQuotes((*wing)->safeGetString("type"))]++;
	}
      }
    }
  }

  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    (*hc)->unsetValue("mobilize");
    Object* flags = (*hc)->safeGetObject("flags");
    if (flags) flags->clear();
    flags = (*hc)->safeGetObject("variables");
    if (flags) flags->clear();
    flags = (*hc)->safeGetObject("modifier");
    if (flags) flags->clear();
  }
}

void WorkerThread::calcCasualties (Object* war) {
  Object* history = war->safeGetObject("history");
  if (!history) return;

  static int globalDate = days(remQuotes(vicGame->safeGetString("date")));
  if (globalDate < 0) {
    static bool printed = false;
    if (printed) return;
    Logger::logStream(Logger::Warning) << "Warning: Cannot calculate time from global date string "
				       << vicGame->safeGetString("date")
				       << ", no war casualties will be assigned. This means neutralities will be very high.\n";
    printed = true;
    return; 
  }
  
  objvec leaves = history->getLeaves();
  double totalWarCasualties = 0;
  double weightedWarCasualties = 0;
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    int battleDate = days((*leaf)->getKey());
    if (0 > battleDate) continue;
    Object* battle = (*leaf)->safeGetObject("battle");
    if (!battle) continue;
    double battleDays = globalDate - battleDate;
    double decay = pow(0.5, battleDays / 365);
    double currCasualties = calcCasualtiesInBattle(battle->safeGetObject("attacker"), decay);
    currCasualties += calcCasualtiesInBattle(battle->safeGetObject("defender"), decay);
    totalWarCasualties += currCasualties;
    weightedWarCasualties += currCasualties*battleDays;     
  }

  double decay = 0;

  if (0 < totalWarCasualties) {
    weightedWarCasualties /= totalWarCasualties; // Average days since infliction
    decay = pow(0.5, weightedWarCasualties / 365);
    Logger::logStream(DebugMisc) << "Dated casualties in " << war->safeGetString("name") << ": " << totalWarCasualties << " " << decay << "\n";
  }
  else {
    // Going to have to use the end date as decay factor
    double enddate = days(remQuotes(war->safeGetString("action")));
    if (0 > enddate) enddate = 365*50;
    else enddate = globalDate - enddate;
    decay = pow(0.5, enddate / 365);
  }

  totalWarCasualties = 0;
  weightedWarCasualties = 0;
  objvec battles = history->getValue("battle");
  for (objiter battle = battles.begin(); battle != battles.end(); ++battle) {
    Object* attacker = (*battle)->safeGetObject("attacker");
    double currCasualties = calcCasualtiesInBattle(attacker, decay);
    totalWarCasualties += currCasualties;
    weightedWarCasualties += currCasualties * decay;
    attacker = (*battle)->safeGetObject("defender");
    currCasualties = calcCasualtiesInBattle(attacker, decay);
    totalWarCasualties += currCasualties;
    weightedWarCasualties += currCasualties * decay;
  }
  Logger::logStream(DebugMisc) << "Weighted total casualties in " << war->safeGetString("name") << ": "
			       << totalWarCasualties << " "
			       << weightedWarCasualties << " "
			       << decay << "\n";
}

double WorkerThread::calcCasualtiesInBattle(Object* battle, double decay) {
  if (!battle) return 0; 
  setPointersFromVicTag(remQuotes(battle->safeGetString("country")));
  if (!vicCountry) return 0;
  double losses = battle->safeGetFloat("losses");
  if (0 > losses) return 0; // This should never happen, but I've seen it in real saves, so...
  vicCountry->resetLeaf("weightedCasualties", losses*decay + vicCountry->safeGetFloat("weightedCasualties"));
  return losses; 
}

double WorkerThread::calcForceLimit (Object* navalBase) {
  if (!navalBase) return 0;
  int level = (int) floor(0.5 + navalBase->tokenAsFloat(0));
  if (0 == level) return 0;
  static int base_naval_support = configObject->safeGetInt("base_naval_support", 10); 
  return base_naval_support * pow(2, level - 1); 
}

void WorkerThread::initialiseVicSummaries () {  
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    setPointersFromVicProvince(*vp);    
    objvec leaves = (*vp)->getLeaves();
    int totalPop = 0;
    for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
      int popSize = (*leaf)->safeGetInt("size", -1);
      if (0 > popSize) continue;
      popIdMap[(*leaf)->safeGetString("id")] = (*leaf); 
      totalPop += popSize;
      (*vp)->resetLeaf((*leaf)->getKey(), popSize + (*vp)->safeGetInt((*leaf)->getKey()));
      if (vicCountry) {
	vicCountry->resetLeaf((*leaf)->getKey(), popSize + vicCountry->safeGetInt((*leaf)->getKey()));
	vicCountry->resetLeaf("totalPop", popSize + vicCountry->safeGetInt("totalPop"));
	vicCountry->resetLeaf("avg_mil", (*leaf)->safeGetFloat("mil") + vicCountry->safeGetFloat("avg_mil"));
      }
    }
    (*vp)->resetLeaf("totalPop", totalPop);
    if (!vicCountry) continue;
    
    Object* navalBase = (*vp)->safeGetObject("naval_base");
    int level = 0;
    if (navalBase) {
      level = (int) floor(0.5 + navalBase->tokenAsFloat(0));
      vicCountry->resetLeaf("navy_limit", vicCountry->safeGetInt("navy_limit") + calcForceLimit(navalBase));
      vicCountry->resetLeaf("naval_base", level + vicCountry->safeGetInt("naval_base"));
    }

    Object* fort = (*vp)->safeGetObject("fort");
    if (fort) {
      level = (int) floor(0.5 + fort->tokenAsFloat(0));
      vicCountry->resetLeaf("fort", level + vicCountry->safeGetInt("fort"));
    }
  }

  double maxArmy = 1;
  double maxNavy = 1;
  Object* vicShipWeights = configObject->getNeededObject("vicShips");
  Object* unitTypes = configObject->getNeededObject("unitTypes");  
  static map<string, bool> printed; 

  map<string, bool> warIndustries;
  map<string, bool> heavyIndustries;
  Object* warFactories = configObject->getNeededObject("war_industry");
  for (int i = 0; i < warFactories->numTokens(); ++i) warIndustries[warFactories->getToken(i)] = true;
  warFactories = configObject->getNeededObject("heavy_industry");
  for (int i = 0; i < warFactories->numTokens(); ++i) heavyIndustries[warFactories->getToken(i)] = true;

  objvec vicTechs = vicTechObject->getLeaves();
  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {
    Object* techs = (*vc)->getNeededObject("technology");
    double throughputMod = 0;
    for (objiter vTech = vicTechs.begin(); vTech != vicTechs.end(); ++vTech) {
      double currMod = (*vTech)->safeGetFloat("factory_throughput");
      if (0.00001 > currMod) continue;
      Object* hasTech = techs->safeGetObject((*vTech)->getKey());
      if (!hasTech) continue;
      if (0.5 > hasTech->tokenAsFloat(0)) continue;
      throughputMod += currMod;
    }
    (*vc)->resetLeaf("throughputBonus", throughputMod); 
    
    (*vc)->resetLeaf("avg_mil", (*vc)->safeGetFloat("avg_mil") / (1 + (*vc)->safeGetInt("totalPop")));
    objvec armies = (*vc)->getValue("army");
    double totalArmy = 0;
    double totalWing = 0;
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regiments = (*army)->getValue("regiment");
      for (objiter regiment = regiments.begin(); regiment != regiments.end(); ++regiment) {
	Object* pop = (*regiment)->safeGetObject("pop");
	if (!pop) continue;
	pop = popIdMap[pop->safeGetString("id")];
	if (!pop) continue;
	string regType = (*regiment)->safeGetString("type");
	if (pop->getKey() != "soldiers") regType = "reserve";
	vicCountryToUnitsMap[*vc][regType].push_back(*regiment);
	(*vc)->resetLeaf(regType, (*vc)->safeGetInt(regType) + 1);
	vicUnitTypes[regType]++;
	if (regType == "plane")	totalWing += (*regiment)->safeGetFloat("strength");
	else totalArmy += (*regiment)->safeGetFloat("strength");
	
	objvec conversions = unitTypes->getValue(regType);
	for (objiter con = conversions.begin(); con != conversions.end(); ++con) {
	  vicUnitsThatConvertToHoIUnits[(*con)->getLeaf()]++; 
	}
	string vicLocation = (*army)->safeGetString("location");
	(*regiment)->resetLeaf("location", vicLocation); 
      }
    }
    (*vc)->resetLeaf("wing_size", totalWing);
    (*vc)->resetLeaf("army_size", totalArmy);
    if (totalArmy > maxArmy) maxArmy = totalArmy;

    double totalNavy = 0;
    objvec navies = (*vc)->getValue("navy");
    double largestNavySize = 0; 
    for (objiter navy = navies.begin(); navy != navies.end(); ++navy) {
      objvec ships = (*navy)->getValue("ship");
      for (objiter ship = ships.begin(); ship != ships.end(); ++ship) {
	double weight = -1;
	string shiptype = remQuotes((*ship)->safeGetString("type"));
	(*vc)->resetLeaf(shiptype, 1 + (*vc)->safeGetInt(shiptype)); 
	weight = vicShipWeights->safeGetFloat(shiptype, -1);
	if (-1 == weight) {
	  weight = 1;
	  if (!printed[shiptype]) {
	    Logger::logStream(Logger::Warning) << "Warning: Unknown Vic ship type " << shiptype << ", assigning weight 1.\n";
	    printed[shiptype] = true;
	  }
	}
	totalNavy += weight;
	if (weight < largestNavySize) continue;
	(*vc)->resetLeaf("largestNavyName", (*navy)->safeGetString("name"));
	largestNavySize = weight; 
      }
    }
    (*vc)->resetLeaf("navy_size", totalNavy);
    if (totalNavy > maxNavy) maxNavy = totalNavy;

    objvec states = (*vc)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      objvec buildings = (*state)->getValue("state_buildings");
      
      for (objiter f = buildings.begin(); f != buildings.end(); ++f) {
	if (0 >= (*f)->safeGetInt("level")) continue;
	string factoryType = remQuotes((*f)->safeGetString("building"));	
	Object* employment = (*f)->safeGetObject("employment");
	if (!employment) continue;
	employment = employment->safeGetObject("employees");
	if (!employment) continue;
	objvec employees = employment->getLeaves();
	int workers = 0; 
	for (objiter emp = employees.begin(); emp != employees.end(); ++emp) {
	  int current = (*emp)->safeGetInt("count");
	  workers += current;
	  if (0 == current) continue;
	  Object* provinceObject = (*emp)->safeGetObject("province_pop_id");
	  if (!provinceObject) continue;
	  string provTag = provinceObject->safeGetString("province_id");
	  Object* vicProv = vicProvIdToVicProvMap[provTag];
	  if (!vicProv) continue;
	  vicProv->resetLeaf(factoryType, current + vicProv->safeGetInt(factoryType));
	  if (6 == provinceObject->safeGetInt("type")) (*f)->resetLeaf("clerks", (*f)->safeGetInt("clerks") + current);
	  else if (7 == provinceObject->safeGetInt("type")) (*f)->resetLeaf("craftsmen", (*f)->safeGetInt("craftsmen") + current);
	}

	(*vc)->resetLeaf(factoryType, workers + (*vc)->safeGetInt(factoryType));
	(*vc)->resetLeaf("total_industry", workers + (*vc)->safeGetInt("total_industry"));
	if (warIndustries[factoryType]) (*vc)->resetLeaf("war_industry", workers + (*vc)->safeGetInt("war_industry"));
	if (heavyIndustries[factoryType]) (*vc)->resetLeaf("heavy_industry", workers + (*vc)->safeGetInt("heavy_industry"));
      }
    }
  }
}

void WorkerThread::loadFiles () {
  provinceMapObject   = loadTextFile(targetVersion + "province_mappings.txt");
  countryMapObject    = loadTextFile(sourceVersion + "country_mappings.txt");
  provinceNamesObject = loadTextFile(targetVersion + "provNames.txt"); 
  
  string customFile = configObject->safeGetString("custom", "NOCUSTOM");
  if (customFile != "NOCUSTOM") customObject = loadTextFile(sourceVersion + customFile);

}

void WorkerThread::setupDebugLog () {
  string debuglog = configObject->safeGetString("logfile");
  if (debuglog != "") {
    string outputdir = "Output\\";
    debuglog = outputdir + debuglog;
    Logger::logStream(Logger::Game) << "Opening debug log " << debuglog << "\n"; 
    
    DWORD attribs = GetFileAttributesA(debuglog.c_str());
    if (attribs != INVALID_FILE_ATTRIBUTES) {
      int error = remove(debuglog.c_str());
      if (0 != error) Logger::logStream(Logger::Warning) << "Warning: Could not delete old log file. New one will be appended.\n";
    }
    parentWindow->openDebugLog(debuglog);
  }
}

/******************************* End initialisers *******************************/ 


/******************************* Begin conversions ********************************/

bool WorkerThread::convertBuildings () {
  Logger::logStream(Logger::Game) << "Beginning building conversion.\n";
  double hoiNavalBases = 0;
  vector<double> infraValues;
  vector<double> aaValues;
  vector<double> airBaseValues;
  vector<double> victoryValues; 
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    double points = (*hp)->safeGetFloat("points", -1);
    if (0.1 < points) {
      victoryValues.push_back(points);
      (*hp)->resetLeaf("points", "0"); 
    }
			    
    Object* infra = (*hp)->safeGetObject("infra");
    if (infra) infraValues.push_back(infra->tokenAsFloat(0)); 

    infra = (*hp)->safeGetObject("anti_air");
    if (infra) {
      aaValues.push_back(infra->tokenAsFloat(0));
      (*hp)->unsetValue("anti_air"); 
    }

    infra = (*hp)->safeGetObject("air_base");
    if (infra) {
      airBaseValues.push_back(infra->tokenAsFloat(0));
      (*hp)->unsetValue("air_base"); 
    }
    
    
    (*hp)->unsetValue("land_fort");
    Object* seaFort = (*hp)->safeGetObject("coastal_fort");
    if (seaFort) (*hp)->setLeaf("coastal", "yes");
    (*hp)->unsetValue("coastal_fort");
    Object* naval_base = (*hp)->safeGetObject("naval_base");
    if (naval_base) {
      (*hp)->setLeaf("coastal", "yes");
      (*hp)->unsetValue("naval_base");
      hoiNavalBases += naval_base->tokenAsFloat(0); 
    }
    else if ((*hp)->safeGetString("coastal", "no") == "no") {
      Object* pos = hoiProvincePositions[(*hp)->getKey()];
      if (!pos) continue;
      naval_base = pos->safeGetObject("naval_base");
      if (!naval_base) {
	pos = pos->safeGetObject("building_position");
	if (!pos) continue;
	naval_base = pos->safeGetObject("naval_base");
      }
      if (naval_base) (*hp)->setLeaf("coastal", "probably"); 
    }
  }

  double vicNavalBases = 0;
  Object* victoryObject = configObject->getNeededObject("victoryPoints");
  objvec victories = victoryObject->getLeaves();
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    bool isCap = ((*vp)->safeGetString("isCapital", "no") == "yes");
    Object* naval_base = (*vp)->safeGetObject("naval_base");
    double nbase = 0;
    if (naval_base) {
      nbase = naval_base->tokenAsFloat(0);
      vicNavalBases += nbase;
    }

    double urbanity      = (*vp)->safeGetFloat("clerks");
    urbanity            /= (1 + (*vp)->safeGetFloat("labourers") + (*vp)->safeGetFloat("farmers"));
    if (isCap) urbanity *= 2; 
    (*vp)->setLeaf("urbanity", urbanity + (*vp)->safeGetFloat("infrastructure"));

    
    double airValue = (*vp)->safeGetFloat("aeroplane_factory");
    airValue      += 100*pow(2, nbase) * (0 < nbase ? 1 : 0);
    airValue      += 100*urbanity;
    (*vp)->resetLeaf("airValue", airValue);
    
    double victoryValue = victoryObject->safeGetFloat("navalBase")*pow(2, nbase) * (0 < nbase ? 1 : 0);
    for (objiter victory = victories.begin(); victory != victories.end(); ++victory) {
      victoryValue += (*vp)->safeGetFloat((*victory)->getKey()) * victoryObject->safeGetFloat((*victory)->getKey());
    }
    if (isCap) victoryValue *= victoryObject->safeGetFloat("isCapital");
    (*vp)->resetLeaf("victoryValue", victoryValue); 
  }

  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    Object* naval_base = (*vp)->safeGetObject("naval_base");
    if (naval_base) {
      setPointersFromVicProvince(*vp);
      Object* hoiTarget = 0;
      for (objiter hp = vicProvToHoiProvsMap[*vp].begin(); hp != vicProvToHoiProvsMap[*vp].end(); ++hp) {
	if (remQuotes((*hp)->safeGetString("owner")) != hoiTag) {
	  Logger::logStream(DebugBuildings) << "Skipping naval base in " << (*hp)->getKey() << " for bad owner " << (*hp)->safeGetString("owner") << "\n"; 
	  continue;
	}
	if ((*hp)->safeGetString("coastal", "no") == "yes") {
	  hoiTarget = (*hp);
	  break;
	}
	if ((*hp)->safeGetString("coastal", "no") == "probably") {
	  hoiTarget = (*hp); // We may find a better - don't break.
	}
      }
      if (!hoiTarget) {
	Logger::logStream(DebugBuildings) << "Could not find coastal HoI province with same owner ("
					  << hoiTag
					  << ") for Vic province " << nameAndNumber(*vp)
					  << ", skipping naval base.\n";
	continue; 
      }
      double hoiLevel = naval_base->tokenAsFloat(0);
      hoiLevel /= vicNavalBases;
      hoiLevel *= hoiNavalBases;
      hoiLevel = floor(0.5 + hoiLevel);
      if (1 > hoiLevel) hoiLevel = 1;
      
      Object* hoiBase = new Object("naval_base");
      hoiBase->addToList(hoiLevel);
      hoiBase->addToList(hoiLevel);
      hoiTarget->setValue(hoiBase);      
    }
  }

  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    Object* fort = (*vp)->safeGetObject("fort");
    if (!fort) continue;
    setPointersFromVicProvince(*vp);
    double level = fort->tokenAsFloat(0);
    level -= 2;
    if (level < 0.1) continue; 
    Object* hoiFort = new Object("land_fort");
    hoiFort->addToList(level);
    hoiFort->addToList(level);
    Object* seaFort = new Object("coastal_fort");
    seaFort->addToList(level);
    seaFort->addToList(level);
    for (objiter hp = vicProvToHoiProvsMap[*vp].begin(); hp != vicProvToHoiProvsMap[*vp].end(); ++hp) {
      if (remQuotes((*hp)->safeGetString("owner")) != hoiTag) {
	Logger::logStream(DebugBuildings) << "Skipping fort in " << (*hp)->getKey() << " for bad owner " << (*hp)->safeGetString("owner") << "\n"; 
	continue;
      }
      (*hp)->setValue(hoiFort);
      if ((*hp)->safeGetString("coastal", "no") == "no") continue;
      (*hp)->setValue(seaFort); 
    }
  }

  // Ascending order
  sort(infraValues.begin(), infraValues.end());
  sort(victoryValues.begin(), victoryValues.end());  
  sort(aaValues.begin(), aaValues.end());
  sort(airBaseValues.begin(), airBaseValues.end()); 
  double leastInfra = infraValues[0]; 
  sort(vicProvinces.begin(), vicProvinces.end(), ObjectDescendingSorter("urbanity"));
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    setPointersFromVicProvince(*vp);
    if (!hoiCountry) continue; 
    for (objiter hp = vicProvToHoiProvsMap[*vp].begin(); hp != vicProvToHoiProvsMap[*vp].end(); ++hp) {
      if ((*hp)->safeGetString("infraSet", "no") == "yes") continue; 
      Object* hoiInfra = (*hp)->safeGetObject("infra");
      double infraToUse = leastInfra;
      if (!hoiInfra) {
	Logger::logStream(DebugBuildings) << "HoI " << nameAndNumber(*hp) << " has no infra in input. ";
	if ((*hp)->safeGetString("owner", "NONE") == "NONE") Logger::logStream(DebugBuildings) << " Is it a sea province?\n";
	else {
	  Logger::logStream(DebugBuildings) << " Assigning lowest infra value " << leastInfra << ".\n";
	  hoiInfra = new Object("infra");
	  (*hp)->setValue(hoiInfra);
	  hoiInfra->addToList(leastInfra);
	  hoiInfra->addToList(leastInfra);
	}
      continue;
      }
      if (remQuotes((*hp)->safeGetString("owner")) != hoiTag) {
	Logger::logStream(DebugBuildings) << "Skipping infra in " << (*hp)->getKey() << " for bad owner " << (*hp)->safeGetString("owner") << "\n"; 
	continue;
      }

      if (0 < infraValues.size()) {
	infraToUse = infraValues.back();
	infraValues.pop_back();
      }
      else {
	static bool printed = false;
	if (!printed) {
	  printed = true;
	  Logger::logStream(DebugBuildings) << "Ran out of HoI infra values at " << (*hp)->getKey() << ", using " << leastInfra << " from here.\n";
	}
      }

      sprintf(stringbuffer, "%.3f", infraToUse);
      Logger::logStream(DebugBuildings) << "HoI " << nameAndNumber(*hp) << " gets infra " << stringbuffer << " from Vic " << nameAndNumber(*vp) << ".\n"; 
      hoiInfra->resetToken(0, stringbuffer);
      hoiInfra->resetToken(1, stringbuffer);
      (*hp)->setLeaf("infraSet", "yes");

      if (0 < aaValues.size()) {
	infraToUse = aaValues.back();
	aaValues.pop_back();
	sprintf(stringbuffer, "%.3f", infraToUse);
	Object* antiAir = (*hp)->getNeededObject("anti_air");
	antiAir->addToList(stringbuffer);
	antiAir->addToList(stringbuffer);
      }
    }
  }

  sort(vicProvinces.begin(), vicProvinces.end(), ObjectDescendingSorter("airValue"));
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    if (0 == airBaseValues.size()) break;
    setPointersFromVicProvince(*vp);
    if (!vicCountry) continue;
    double baseValue = airBaseValues.back();
    Object* target = 0;
    double bestIndustry = -1;
    for (objiter hp = vicProvToHoiProvsMap[*vp].begin(); hp != vicProvToHoiProvsMap[*vp].end(); ++hp) {
      if (remQuotes((*hp)->safeGetString("owner")) != hoiTag) continue;
      if ((*hp)->safeGetObject("air_base")) continue;
      double currIndustry = 0;
      Object* industry = (*hp)->safeGetObject("industry");
      if (industry) currIndustry = industry->tokenAsFloat(0);
      if (currIndustry < bestIndustry) continue;
      target = (*hp);
      bestIndustry = currIndustry;
    }
    if (!target) {
      Logger::logStream(DebugBuildings) << "Could not find HoI province to put airbase of Vic " << nameAndNumber(*vp) << " in, skipping.\n";
      continue;
    }
    Object* airBase = target->getNeededObject("air_base");
    airBase->addToList(baseValue);
    airBase->addToList(baseValue);
    airBaseValues.pop_back(); 
  }

  sort(vicProvinces.begin(), vicProvinces.end(), ObjectDescendingSorter("victoryValue"));
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    if (0 == victoryValues.size()) break;
    setPointersFromVicProvince(*vp);
    if (!vicCountry) continue;
    double baseValue = victoryValues.back();
    Object* target = 0;
    double bestIndustry = -1;
    for (objiter hp = vicProvToHoiProvsMap[*vp].begin(); hp != vicProvToHoiProvsMap[*vp].end(); ++hp) {
      if (remQuotes((*hp)->safeGetString("owner")) != vicTag) continue;
      if ((*hp)->safeGetObject("air_base")) continue;
      double currIndustry = 0;
      Object* industry = (*hp)->safeGetObject("industry");
      if (industry) currIndustry = industry->tokenAsFloat(0);
      if (currIndustry < bestIndustry) continue;
      target = (*hp);
      bestIndustry = currIndustry;
    }
    if (!target) continue;
    target->resetLeaf("points", baseValue);
    victoryValues.pop_back(); 
  } 

  Object* strategic = configObject->getNeededObject("strategic_provinces");
  for (int i = 0; i < strategic->numTokens(); ++i) {
    string hoiProvId = strategic->getToken(i);
    Object* hoiProv = hoiProvIdToHoiProvMap[hoiProvId];
    if (!hoiProv) {
      Logger::logStream(DebugBuildings) << "Warning: Could not find strategic province " << hoiProvId << ".\n";
      continue;
    }
    if (0.1 < hoiProv->safeGetFloat("points")) continue;
    hoiProv->resetLeaf("points", "1");
    Logger::logStream(DebugBuildings) << nameAndNumber(hoiProv) << " assigned a victory point for strategic-ness.\n"; 
  }
  
  for (objiter hc = hoiCountries.begin(); hc != hoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    if (!vicCountry) continue;
    string hoiCapId = hoiCountry->safeGetString("capital");
    Object* hoiCap = hoiProvIdToHoiProvMap[hoiCapId];
    if (!hoiCap) {
      // This is a bit of a problem, eh?
      Logger::logStream(DebugBuildings) << "Warning: HoI country " << hoiTag << " does not seem to have a capital; the id is \"" << hoiCapId << "\".\n";
      continue;
    }
    if (0.1 > hoiCap->safeGetFloat("points")) hoiCap->resetLeaf("points", "1"); 
    
    if (hoiCap->safeGetObject("air_base")) continue;
    Object* airBase = hoiCap->getNeededObject("air_base");
    airBase->addToList("1.000");
    airBase->addToList("1.000");
  }
  
  Logger::logStream(Logger::Game) << "Done with building conversion.\n";
  return true;
}

bool WorkerThread::convertDiplomacy () {
  Object* hoiDip = hoiGame->getNeededObject("diplomacy");
  hoiDip->clear();

  for (objiter hc1 = allHoiCountries.begin(); hc1 != allHoiCountries.end(); ++hc1) {
    for (objiter hc2 = allHoiCountries.begin(); hc2 != allHoiCountries.end(); ++hc2) {
      if ((*hc1) == (*hc2)) continue;
      if (0 == hoiCountryToHoiProvsMap[*hc2].size()) {
	(*hc1)->unsetValue((*hc2)->getKey());
	continue; 
      }
    }
  }
  Object* factionList = hoiGame->safeGetObject("faction");
  if (factionList) {
    objvec factions = factionList->getLeaves();
    for (objiter faction = factions.begin(); faction != factions.end(); ++faction) {
      (*faction)->clear(); 
    }
  }
  Object* victory = hoiGame->safeGetObject("victory_conditions");
  if (victory) {
    factionList = victory->safeGetObject("faction");
    if (factionList) {
      objvec factions = factionList->getLeaves();
      for (objiter faction = factions.begin(); faction != factions.end(); ++faction) {
	(*faction)->clear(); 
      }
    }
  }

  Object* vicDip = vicGame->getNeededObject("diplomacy");
  objvec vicAlliances = vicDip->getValue("alliance");
  for (objiter val = vicAlliances.begin(); val != vicAlliances.end(); ++val) {
    string vicFirst = remQuotes((*val)->safeGetString("first"));
    setPointersFromVicTag(vicFirst);
    if (!hoiCountry) continue;
    string hoiFirst = hoiTag;

    string vicSecond = remQuotes((*val)->safeGetString("second"));
    setPointersFromVicTag(vicSecond);
    if (!hoiCountry) continue;
    string hoiSecond = hoiTag;

    Object* hoiAlliance = new Object("alliance");
    hoiDip->setValue(hoiAlliance);
    hoiAlliance->setLeaf("first", addQuotes(hoiFirst));
    hoiAlliance->setLeaf("second", addQuotes(hoiSecond));
    string startDate = remQuotes((*val)->safeGetString("start_date"));
    startDate += ".0";
    hoiAlliance->setLeaf("start_date", addQuotes(startDate));

    Logger::logStream(DebugDiplomacy) << "HoI " << hoiFirst << " and " << hoiSecond << " are allied due to alliance of Vicky "
				      << vicFirst << " and " << vicSecond << ".\n"; 
  }

  objvec vicVassals = vicDip->getValue("vassal");
  for (objiter val = vicVassals.begin(); val != vicVassals.end(); ++val) {
    string vicFirst = remQuotes((*val)->safeGetString("first"));
    setPointersFromVicTag(vicFirst);
    if (!hoiCountry) continue;
    string hoiFirst = hoiTag;

    string vicSecond = remQuotes((*val)->safeGetString("second"));
    setPointersFromVicTag(vicSecond);
    if (!hoiCountry) continue;
    string hoiSecond = hoiTag;

    Object* hoiAlliance = new Object("vassal");
    hoiDip->setValue(hoiAlliance);
    hoiAlliance->setLeaf("first", addQuotes(hoiFirst));
    hoiAlliance->setLeaf("second", addQuotes(hoiSecond));
    string startDate = remQuotes((*val)->safeGetString("start_date"));
    startDate += ".0";
    hoiAlliance->setLeaf("start_date", addQuotes(startDate));

    Logger::logStream(DebugDiplomacy) << "HoI " << hoiSecond << " is vassal of " << hoiFirst << " because Vicky "
				      << vicSecond << " is vassal to " << vicFirst << ".\n"; 
  }

  hoiGame->unsetValue("active_war");
  hoiGame->unsetValue("previous_war"); 

  objvec wars = vicGame->getValue("previous_war");
  for (objiter war = wars.begin(); war != wars.end(); ++war) {
    calcCasualties(*war);
  }
  
  wars = vicGame->getValue("active_war");
  for (objiter war = wars.begin(); war != wars.end(); ++war) {
    calcCasualties(*war); 
    // Create the HoI war
    Object* hoiWar = new Object("active_war");
    hoiWar->setValue((*war)->safeGetObject("name"));
    hoiWar->setLeaf("limited", "no"); 
    hoiWar->setValue((*war)->safeGetObject("action"));
    objvec vicAttackers = (*war)->getValue("attacker");
    objvec vicDefenders = (*war)->getValue("defender");    
    for (objiter vat = vicAttackers.begin(); vat != vicAttackers.end(); ++vat) {
      setPointersFromVicTag(remQuotes((*vat)->getLeaf()));
      if (!hoiCountry) continue;
      hoiWar->setLeaf("attacker", addQuotes(hoiTag));
      vicCountry->resetLeaf("at_war", "yes");
      for (objiter vdf = vicDefenders.begin(); vdf != vicDefenders.end(); ++vdf) {
	string defTag = remQuotes((*vdf)->getLeaf());
	Object* relation = vicCountry->getNeededObject(defTag);
	relation->resetLeaf("war", "yes"); 
      }
    }
    
    for (objiter vdf = vicDefenders.begin(); vdf != vicDefenders.end(); ++vdf) {
      setPointersFromVicTag(remQuotes((*vdf)->getLeaf()));
      if (!hoiCountry) continue;
      hoiWar->setLeaf("defender", addQuotes(hoiTag));
      vicCountry->resetLeaf("at_war", "yes");
      for (objiter vat = vicAttackers.begin(); vat != vicAttackers.end(); ++vat) {
	string attTag = remQuotes((*vat)->getLeaf()); 
	Object* relation = vicCountry->getNeededObject(attTag);
	relation->resetLeaf("war", "yes"); 
      }
    }
    if (0 == hoiWar->getValue("attacker").size()) continue;
    if (0 == hoiWar->getValue("defender").size()) continue;
    hoiGame->setValue(hoiWar);

    string org = (*war)->safeGetString("original_attacker");
    if (org == "\"---\"") hoiWar->setLeaf("original_attacker", org);
    else {
      setPointersFromVicTag(remQuotes(org));
      if (hoiCountry) hoiWar->setLeaf("original_attacker", addQuotes(hoiTag));
      else hoiWar->setLeaf("original_attacker", "\"---\"");
    }
    org = (*war)->safeGetString("original_defender");
    if (org == "\"---\"") hoiWar->setLeaf("original_defender", org);
    else {
      setPointersFromVicTag(remQuotes(org));
      if (hoiCountry) hoiWar->setLeaf("original_defender", addQuotes(hoiTag));
      else hoiWar->setLeaf("original_defender", "\"---\"");
    }    
  }

  double maxWarThreat = configObject->safeGetFloat("maxWarThreat", 50);
  double maxPaxThreat = configObject->safeGetFloat("maxPaxThreat", 25);
  
  for (objiter hc1 = allHoiCountries.begin(); hc1 != allHoiCountries.end(); ++hc1) {
    setPointersFromHoiCountry(*hc1);
    Object* vc1 = vicCountry;
    Object* highestThreat = 0; 
    for (objiter hc2 = allHoiCountries.begin(); hc2 != allHoiCountries.end(); ++hc2) {
      Object* relation = (*hc1)->getNeededObject((*hc2)->getKey());
      relation->resetLeaf("relation", "0.000");
      relation->resetLeaf("threat", "0.000");

      if (!vc1) continue;      
      setPointersFromHoiCountry(*hc2);
      if (!vicCountry) continue;
      if (vicTag == "REB") continue; 
      if (vc1 == vicCountry) continue;
      if ((*hc1)->getKey() == "REB") continue;
      
      bool war = false;
      Object* vicRelation = vc1->safeGetObject(vicTag);
      if (vicRelation) {
	relation->resetLeaf("value", vicRelation->safeGetString("value"));
	if (vicRelation->safeGetString("war", "no") == "yes") war = true;
      }

      double armyRatio = vicCountry->safeGetFloat("infantry");
      armyRatio /= (1 + vc1->safeGetFloat("infantry"));
      
      double threat = 0;
      if      (armyRatio < 0.5) threat = 0;
      else if (armyRatio < 1.0) threat = (armyRatio - 0.5)*0.33;
      else if (armyRatio < 1.5) threat = (0.5*0.33 + armyRatio-1);
      else                      threat = (0.5*0.83 + (1-0.5*0.83)*(2/M_PI)*atan(armyRatio-1.5));
      threat *= war ? maxWarThreat : maxPaxThreat;

      if (!war) threat *= (2/M_PI)*atan((1+vicCountry->safeGetFloat("badboy") / (1+vc1->safeGetFloat("badboy"))) - 1);

      sprintf(stringbuffer, "%.3f", threat);
      relation->resetLeaf("threat", stringbuffer);
      if ((!highestThreat) || (threat > highestThreat->safeGetFloat("threat"))) highestThreat = relation;
    }
    if (highestThreat) (*hc1)->resetLeaf("highest_threat", addQuotes(highestThreat->getKey()));
    else (*hc1)->resetLeaf("highest_threat", "\"---\""); 
  }
  
  return true; 
}

bool WorkerThread::convertGovernments () {
  Logger::logStream(Logger::Game) << "Beginning government conversion.\n";
  objvec resemblances;

  map<string, Object*> govMap; 
  for (objiter oc = allHoiCountries.begin(); oc != allHoiCountries.end(); ++oc) {
    Object* government = new Object("government");
    govMap[(*oc)->getKey()] = government;
    government->setValue((*oc)->safeGetObject("ministers"));
    government->setLeaf("government", (*oc)->safeGetString("government"));
    government->setLeaf("head_of_state", (*oc)->safeGetString("head_of_state"));
    government->setLeaf("head_of_government", (*oc)->safeGetString("head_of_government"));
    government->setLeaf("foreign_minister", (*oc)->safeGetString("foreign_minister"));
    government->setLeaf("armament_minister", (*oc)->safeGetString("armament_minister"));
    government->setLeaf("minister_of_security", (*oc)->safeGetString("minister_of_security"));
    government->setLeaf("minister_of_intelligence", (*oc)->safeGetString("minister_of_intelligence"));
    government->setLeaf("chief_of_staff", (*oc)->safeGetString("chief_of_staff"));
    government->setLeaf("chief_of_army", (*oc)->safeGetString("chief_of_army"));
    government->setLeaf("chief_of_navy", (*oc)->safeGetString("chief_of_navy"));
    government->setLeaf("chief_of_air", (*oc)->safeGetString("chief_of_air"));
    government->setLeaf("election", (*oc)->safeGetString("election"));
    government->setLeaf("last_election", (*oc)->safeGetString("last_election"));    
  }
  
  for (objiter nc = hoiCountries.begin(); nc != hoiCountries.end(); ++nc) {
    setPointersFromHoiCountry(*nc);
    for (objiter oc = allHoiCountries.begin(); oc != allHoiCountries.end(); ++oc) {
      if ((*oc)->getKey() == "REB") continue;
      Object* resemblance = new Object("resemblance");
      resemblance->resetLeaf("newCountry", hoiTag);
      resemblance->resetLeaf("oldCountry", (*oc)->getKey());
      double value = calculateGovResemblance(vicCountry, (*oc));
      resemblance->resetLeaf("value", value); 
      resemblances.push_back(resemblance);
      if (value > 0.01) Logger::logStream(DebugGovernments) << "Vic " << vicCountry->getKey() << " resembles HoI " << (*oc)->getKey() << " at " << value << ".\n"; 
    }
  }
  
  ObjectDescendingSorter sorter("value");
  sort(resemblances.begin(), resemblances.end(), sorter);
  map<string, string> mappedCountries;
  for (objiter r = resemblances.begin(); r != resemblances.end(); ++r) {
    hoiTag = (*r)->safeGetString("newCountry");
    if (mappedCountries[hoiTag] != "") continue;
    hoiCountry = hoiTagToHoiCountryMap[hoiTag];
    if (0 == hoiCountryToHoiProvsMap[hoiCountry].size()) continue; 
    string oldTag = (*r)->safeGetString("oldCountry");
    Object* newGov = govMap[oldTag];
    if (newGov->safeGetString("used", "no") == "yes") continue;
    setPointersFromHoiCountry(hoiCountry); 
    newGov->resetLeaf("used", "yes");
    mappedCountries[hoiTag] = oldTag; 
    Logger::logStream(DebugGovernments) << "Vic country "
					<< vicTag
					<< " (HoI "
					<< hoiTag
					<< ") getting government of historical "
					<< oldTag 
					<< " from resemblance "
					<< (*r)->safeGetString("value")
					<< ".\n";
    swap(newGov, hoiCountry, "ministers");
    swap(newGov, hoiCountry, "government");
    swap(newGov, hoiCountry, "head_of_state");
    swap(newGov, hoiCountry, "head_of_government");
    swap(newGov, hoiCountry, "foreign_minister");
    swap(newGov, hoiCountry, "armament_minister");
    swap(newGov, hoiCountry, "minister_of_security");
    swap(newGov, hoiCountry, "minister_of_intelligence");
    swap(newGov, hoiCountry, "chief_of_staff");
    swap(newGov, hoiCountry, "chief_of_army");
    swap(newGov, hoiCountry, "chief_of_navy");
    swap(newGov, hoiCountry, "chief_of_air");
    swap(newGov, hoiCountry, "election");
    swap(newGov, hoiCountry, "last_election");
  }

  Logger::logStream(Logger::Game) << "Done with governments.\n"; 
  return true; 
}

string extractBest (string def, Object* hoiValues, double points) {
  objvec hoiLaws = hoiValues->getLeaves();	
  double bestPointsSoFar = -1e6;
  for (objiter hoiLaw = hoiLaws.begin(); hoiLaw != hoiLaws.end(); ++hoiLaw) {
    double requiredPoints = hoiValues->safeGetFloat((*hoiLaw)->getKey());
    if (points < requiredPoints) continue;
    if (requiredPoints < bestPointsSoFar) continue;
    def = (*hoiLaw)->getKey();
    bestPointsSoFar = requiredPoints;
  }
  return def; 
}

bool WorkerThread::convertLaws () {
  Logger::logStream(Logger::Game) << "Beginning law conversion.\n";

  Object* lawConversions = configObject->safeGetObject("laws");
  if (!lawConversions) {
    Logger::logStream(Logger::Error) << "Error: Could not find laws object in config.txt.\n";
    return false; 
  }

  objvec laws = lawConversions->getLeaves();
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    if (vicCountry) Logger::logStream(DebugLaws) << "Laws for " << vicTag << " (HoI " << hoiTag << "):\n"; 
    for (objiter law = laws.begin(); law != laws.end(); ++law) {
      string base = (*law)->safeGetString("base", "NOLAW");
      string hoiKey = (*law)->getKey();
      if (base == "NOLAW") {
	Logger::logStream(Logger::Error) << "Error: No base provided for law " << hoiKey << ".\n";
	return false; 
      }
      hoiCountry->resetLeaf(hoiKey, base);
      if (!vicCountry) continue;
      if (0 == vicCountryToHoiProvsMap[vicCountry].size()) continue;
      
      string conversion = (*law)->safeGetString("conversionType", "NOT_FOUND");
      if (conversion == "vickyField") {
	string vicKey = (*law)->safeGetString("keyword", "NO_KEYWORD_GIVEN");
	string vicValue = vicCountry->safeGetString(vicKey, "NOT_FOUND_IN_VIC_OBJECT");
	string newHoiValue = (*law)->safeGetString(vicValue, NFCON);
	if (newHoiValue == NFCON) {
	  Logger::logStream(Logger::Warning) << "  Warning: Don't know what to do with Vicky value "
					     << vicKey << " = " << vicValue
					     << ", sticking to base " << base << " for HoI " << hoiKey << ".\n";
	  newHoiValue = base; 
	}
	Logger::logStream(DebugLaws) << "  " << "Vic " << vicKey << " = " << vicValue << " gives HoI " << hoiKey << " = " << newHoiValue << "\n";
	hoiCountry->resetLeaf(hoiKey, newHoiValue); 
      }
      else if (conversion == "points") {
	Object* hoiValues = (*law)->safeGetObject("hoiValues");
	if (!hoiValues) {
	  Logger::logStream(Logger::Warning) << "  Could not find hoiValues, leaving " << hoiKey << " as base " << base << ".\n";
	  continue; 
	}
	Object* vicKeys = (*law)->safeGetObject("vicKeys");
	Object* pointObject = (*law)->getNeededObject("points");
	int points = 0;
	for (int i = 0; i < vicKeys->numTokens(); ++i) {
	  string vicKey = vicKeys->getToken(i);
	  string vicVal = vicCountry->safeGetString(vicKey, "NOT_FOUND_IN_VIC_OBJECT");
	  int p = pointObject->safeGetInt(vicVal);
	  Logger::logStream(DebugLaws) << "  " << p << " points for " << hoiKey << " from " << vicKey << " = " << vicVal << "\n";
	  points += p;
	}
	string finalHoiLaw = extractBest(base, hoiValues, points);
	Logger::logStream(DebugLaws) << "  Total " << points << " gives HoI " << hoiKey << " = " << finalHoiLaw << ".\n";
	hoiCountry->resetLeaf(hoiKey, finalHoiLaw); 
      }
      else if (conversion == "ratio") {
	string numKey = (*law)->safeGetString("numerator", NFCON);
	string denKey = (*law)->safeGetString("denominator", NFCON);
	double vicNum = vicCountry->safeGetInt(numKey);
	int vicDen    = 1 + vicCountry->safeGetInt(denKey);
	vicNum /= vicDen;
	Object* hoiValues = (*law)->safeGetObject("hoiValues");
	if (!hoiValues) {
	  Logger::logStream(Logger::Warning) << "  Could not find hoiValues, leaving " << hoiKey << " as base " << base << ".\n";
	  continue; 
	}
	string finalHoiLaw = extractBest(base, hoiValues, vicNum);
	Logger::logStream(DebugLaws) << "  Ratio " << numKey << "/" << denKey << " = " << vicNum << " gives HoI " << hoiKey << " = " << finalHoiLaw << ".\n";
	hoiCountry->resetLeaf(hoiKey, finalHoiLaw); 	
      }
      else {
	Logger::logStream(Logger::Error) << "Error: Unsupported conversionType \"" << conversion << "\" for law " << hoiKey
					 << ". Supported are vickyField, ratio, and points.\n";
	return false; 
      }
    }
  }

  
  Logger::logStream(Logger::Game) << "Done with law conversion.\n";
  return true;
}

void WorkerThread::getOfficers (objvec& candidates, string keyword, double total, unsigned int original) {
  if (0 == candidates.size()) {
    static map<string, bool> printed;
    if (printed[keyword]) return;
    printed[keyword] = true;
    Logger::logStream(Logger::Warning) << "Warning: Ran out of officers of type " << keyword << "; " << hoiTag << " and subsequent tags do not get any.\n";
    return; 
  }
  double fraction = hoiCountry->safeGetFloat(keyword);
  Logger::logStream(DebugLeaders) << "HoI " << hoiTag << " (Vic " << vicTag << ") has " << fraction << " " << keyword; 
  fraction /= total;
  int toAssign = (int) floor(fraction * original);
  if (0 == toAssign) toAssign = 1;
  Logger::logStream(DebugLeaders) << " giving " << toAssign << " officers.\n";

  Object* leaders = hoiCountry->getNeededObject("active_leaders");
  // Already shuffled
  for (int i = 0; i < toAssign; ++i) {
    leaders->setValue(pop(candidates));
  }
}

bool WorkerThread::convertLeaders () {
  Logger::logStream(Logger::Game) << "Beginning leader conversion.\n";

  objvec landLeaders;
  objvec navyLeaders;
  objvec wingLeaders;
  double totalArmyWeight = 0;
  double totalNavyWeight = 0;
  double totalWingWeight = 0;
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    if ((*hc)->getKey() == "REB") continue;
    Object* lead = (*hc)->safeGetObject("active_leaders");
    if (!lead) continue;
    objvec leaders = lead->getLeaves();
    for (objiter leader = leaders.begin(); leader != leaders.end(); ++leader) {
      string area = leaderTypesObject->safeGetString((*leader)->getKey(), "land");
      if      (area == "land") landLeaders.push_back(*leader);
      else if (area == "sea" ) navyLeaders.push_back(*leader);
      else                     wingLeaders.push_back(*leader);
    }
    lead->clear();

    setPointersFromHoiCountry(*hc);
    double armyWeight = configObject->safeGetFloat("minimumArmyWeight", 75);
    double navyWeight = configObject->safeGetFloat("minimumNavyWeight", 1000);
    double wingWeight = configObject->safeGetFloat("minimumWingWeight", 30);

    if (!vicCountry) continue;
    armyWeight += vicCountry->safeGetFloat("army_size");
    navyWeight += vicCountry->safeGetFloat("trueNavySize");
    wingWeight += vicCountry->safeGetFloat("wing_size");

    (*hc)->resetLeaf("landOfficerWeight", armyWeight);
    (*hc)->resetLeaf("navyOfficerWeight", navyWeight);
    (*hc)->resetLeaf("wingOfficerWeight", wingWeight);

    totalArmyWeight += armyWeight;
    totalNavyWeight += navyWeight;
    totalWingWeight += wingWeight; 
  }

  unsigned int totalLandLeaders = landLeaders.size();
  unsigned int totalNavyLeaders = navyLeaders.size();
  unsigned int totalWingLeaders = wingLeaders.size();
  random_shuffle(landLeaders.begin(), landLeaders.end());
  random_shuffle(navyLeaders.begin(), navyLeaders.end());
  random_shuffle(wingLeaders.begin(), wingLeaders.end());
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    if ((*hc)->getKey() == "REB") continue;
    setPointersFromHoiCountry(*hc);
    getOfficers(landLeaders, "landOfficerWeight", totalArmyWeight, totalLandLeaders);
    getOfficers(navyLeaders, "navyOfficerWeight", totalNavyWeight, totalNavyLeaders);
    getOfficers(wingLeaders, "wingOfficerWeight", totalWingWeight, totalWingLeaders);
  }

  Logger::logStream(Logger::Game) << "Done with leaders.\n"; 
  return true; 
}

bool WorkerThread::convertMisc () {
  Logger::logStream(Logger::Game) << "Starting misc conversion.\n";

  double minimumNeutrality = configObject->safeGetFloat("minimumNeutrality", 10);
  double neutralRate = 100 - minimumNeutrality;
  double neutralityPerCasualty = configObject->safeGetFloat("neutralityPerCasualty");
  double unityRate   = 100 - configObject->safeGetFloat("minimumUnity", 10);

  objvec rebels = vicGame->getValue("rebel_faction");
  for (objiter rebel = rebels.begin(); rebel != rebels.end(); ++rebel) {
    setPointersFromVicTag(remQuotes((*rebel)->safeGetString("country")));
    if (!hoiCountry) continue;
    double totalRebs = 0;
    objvec rebs = (*rebel)->getValue("pop");
    for (objiter reb = rebs.begin(); reb != rebs.end(); ++reb) {
      Object* pop = popIdMap[(*reb)->safeGetString("id")];
      if (!pop) continue;
      totalRebs += pop->safeGetInt("size"); 
    }
    vicCountry->setLeaf("totalRebels", totalRebs); 
  }
  
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    double unity = 100;
    double dissent = 0;
    double neutrality = 100;
    double exhaustion = 0;
    double influence = 0; 
    if (vicCountry) {
      unity = 100 - unityRate*vicCountry->safeGetFloat("avg_mil");
      bool atWar = (vicCountry->safeGetString("at_war", "no") == "yes");
      neutrality = 100 + vicCountry->safeGetFloat("weightedCasualties") * neutralityPerCasualty * (atWar ? -1 : 1) - neutralRate*vicCountry->safeGetFloat("revanchism");
      if (neutrality > 100) neutrality = 100;
      if (neutrality < minimumNeutrality) neutrality = minimumNeutrality; 
      dissent  = vicCountry->safeGetFloat("totalRebels");
      dissent /= (1 + vicCountry->safeGetFloat("totalPop"));
      dissent *= 100;
      exhaustion = 0.1 * vicCountry->safeGetFloat("war_exhaustion");
      influence = vicCountry->safeGetFloat("diplo_influence"); 
      if (0 < vicCountryToHoiProvsMap[vicCountry].size()) {
	Logger::logStream(DebugMisc) << "Vic " << vicTag << " (HoI " << hoiTag << ") gets unity, dissent, neutrality "
				     << unity << ", " << dissent << ", " << neutrality << ".\n";
      }
    }

    sprintf(stringbuffer, "%.3f", neutrality);
    hoiCountry->resetLeaf("neutrality", stringbuffer);
    hoiCountry->resetLeaf("effective_neutrality", stringbuffer);
    sprintf(stringbuffer, "%.3f", unity);
    hoiCountry->resetLeaf("national_unity", stringbuffer);
    sprintf(stringbuffer, "%.3f", dissent);
    hoiCountry->resetLeaf("dissent", stringbuffer);
    sprintf(stringbuffer, "%.3f", exhaustion);
    hoiCountry->resetLeaf("war_exhaustion", stringbuffer);
    sprintf(stringbuffer, "%.3f", influence);
    hoiCountry->resetLeaf("diplo_influence", stringbuffer);    
  }

  
  Logger::logStream(Logger::Game) << "Done with misc.\n"; 
  return true; 
}

Object* createObjectWithIdAndType (int id, int type, string keyword) {
  Object* ret = new Object(keyword);
  setIdAndType(ret, id, type);
  return ret; 
}

Object* WorkerThread::createRegiment (int id, string type, string name, string keyword) {
  static Object* strengths = configObject->getNeededObject("unitStrengths");
  
  Object* ret = createObjectWithIdAndType(id, 41, keyword);
  ret->setLeaf("type", addQuotes(type));
  ret->setLeaf("name", addQuotes(name));
  if (0 < strengths->safeGetFloat(type, -1)) ret->setLeaf("strength", strengths->safeGetString(type)); 
  return ret; 
}

void WorkerThread::makeHigher (objvec& lowHolder, int& numUnits, string name, string location, string keyword, objvec& highHolder) {
  Object* higher = createObjectWithIdAndType(numUnits++, 41, keyword);
  if (keyword != "division") {
    Object* hq = createRegiment(numUnits++, "hq_brigade", remQuotes(name) + " HQ", "regiment");
    //hq->setLeaf("location", location); 
    higher->setValue(hq); 
  } 
  higher->setLeaf("name", name);
  higher->setLeaf("location", location);
  highHolder.push_back(higher); 
  while (0 < lowHolder.size()) higher->setValue(pop(lowHolder));
}

string WorkerThread::selectHoiProvince (string vicLocation, Object* hoiCountry) {
  Object* vicProv = vicProvIdToVicProvMap[vicLocation];
  for (objiter hoiProv = vicProvToHoiProvsMap[vicProv].begin(); hoiProv != vicProvToHoiProvsMap[vicProv].end(); ++hoiProv) {   
    if (NO_OWNER == (*hoiProv)->safeGetString("owner", NO_OWNER)) continue;
    return (*hoiProv)->getKey();
  }
  return hoiCountry->safeGetString("capital"); 
}

bool WorkerThread::convertOoBs () {
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    (*hc)->unsetValue("theatre");
    (*hc)->unsetValue("navy"); 
  }

  Object* unitConversions = configObject->safeGetObject("unitTypes");
  if (!unitConversions) {
    Logger::logStream(Logger::Error) << "Error: Could not find unitTypes object in config.\n";
    return false; 
  }

  Object* extraObject = configObject->getNeededObject("extraUnits");
  objvec extras = extraObject->getLeaves();
  for (objiter extra = extras.begin(); extra != extras.end(); ++extra) {
    if (0 == (*extra)->numTokens()) {
      Logger::logStream(Logger::Warning) << "Warning: Object " << *(*extra) << " is not in expected format {vicUnitType num1 num2 ...}, ignoring.\n";
      continue;
    }
    string hoiUnitType = (*extra)->getKey();
    if (0 == hoiUnitTypes[hoiUnitType]) {
      Logger::logStream(Logger::Warning) << "Warning: Not creating extras of unknown HoI unit type " << hoiUnitType << ".\n";
      continue;
    }
    string vicUnitType = (*extra)->getToken(0);
    int vicUnits = vicUnitTypes[vicUnitType];    
    if (0 == vicUnits) {
      Logger::logStream(Logger::Warning) << "Warning: Not creating extras from unknown Vic unit type " << vicUnitType << ".\n";
      continue;
    }
    int previous = 0;
    int gap = 0;
    int totalExtra = 0;
    for (int i = 1; i < (*extra)->numTokens(); ++i) {
      int current = (*extra)->tokenAsInt(i);
      if (vicUnits < current) break;
      totalExtra++;
      gap = current - previous;
      previous = current; 
    }
    if (0 < gap) {
      while (vicUnits > previous) {
	vicUnits -= gap;
	totalExtra++;
      }
    }
    if (0 < totalExtra) Logger::logStream(DebugUnits) << "Creating " << totalExtra << " additional " << hoiUnitType
						      << " due to " << vicUnits << " " << vicUnitType << ".\n";
    hoiUnitTypes[hoiUnitType] += totalExtra;
  }

  Object* hoiDivisionNames = configObject->getNeededObject("hoiDivNames"); 
  Object* airUnits = configObject->getNeededObject("airUnits");
  map<string, bool> airUnitMap;
  for (int i = 0; i < airUnits->numTokens(); ++i) airUnitMap[airUnits->getToken(i)] = true; 
  
  int numUnits = 1;
  objvec unitTypes = unitConversions->getLeaves();
  Object* reserveObject = configObject->safeGetObject("reserveFractions"); // Created by the converter.
  for (objiter ut = unitTypes.begin(); ut != unitTypes.end(); ++ut) {
    string hoiUnitName = (*ut)->getLeaf();
    double denom = hoiUnitTypes[hoiUnitName];
    if (0.1 > denom) denom = 1;
    double num = reserveObject->safeGetFloat(hoiUnitName);
    if (0.001 > num) continue;
    num /= denom;
    reserveObject->resetLeaf(hoiUnitName, num); 
  }
  Object* reservePenalties = configObject->getNeededObject("reservePenalties");
      
  for (objiter hc = hoiCountries.begin(); hc != hoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    if (hoiTag == "REB") continue;
    string hoiCap = hoiCountry->safeGetString("capital", "NONE");
    if (hoiCap == "NONE") continue; 
    Object* theatre = createObjectWithIdAndType(numUnits++, 41, "theatre");
    hoiCountry->setValue(theatre);
    theatre->setLeaf("name", addQuotes("Home Defense Theatre")); 
    theatre->setLeaf("location", hoiCap);
    theatre->setLeaf("is_prioritized", "no");
    theatre->setLeaf("can_reinforce", "yes");
    theatre->setLeaf("can_upgrade", "yes");
    theatre->setLeaf("fuel", "1.000");
    theatre->setLeaf("supplies", "1.000");
    theatre->setValue(createRegiment(numUnits++, "hq_brigade", "High Command", "regiment"));
    objvec divHolder;
    objvec corHolder;
    objvec armHolder;
    objvec groHolder; 
    int corCounter = 1;
    int armCounter = 1;
    int groCounter = 1;
    bool mobilised = false;
    if (vicCountry->safeGetInt("reserve") > 0) {
      Logger::logStream(DebugUnits) << vicTag << " (HoI " << hoiTag << ") is mobilised.\n";
      mobilised = true;
      hoiCountry->resetLeaf("mobilize", "yes");
    }
    
    string mobLaw = hoiCountry->safeGetString("conscription_law", "volunteer_army");
    double reserveFactor = reservePenalties->safeGetFloat(mobLaw, 0.25); 
    
    for (objiter ut = unitTypes.begin(); ut != unitTypes.end(); ++ut) {
      string vicUnitName = (*ut)->getKey();
      string hoiUnitName = (*ut)->getLeaf();
      double reserveFraction = reserveObject->safeGetFloat(hoiUnitName, 0); 
      double vicUnits = vicCountryToUnitsMap[vicCountry][vicUnitName].size();
      if (0.1 > vicUnits) continue;
      vicUnits /= vicUnitsThatConvertToHoIUnits[hoiUnitName];
      vicUnits *= hoiUnitTypes[hoiUnitName];
      if (0.01 > vicUnits) continue;
      if (1 > vicUnits) vicUnits = 1;
      int unitsToGenerate = (int) floor(vicUnits + 0.5);
      Logger::logStream(DebugUnits) << vicTag << " (HoI " << hoiTag << ") gets "
				    << unitsToGenerate << " " << hoiUnitName
				    << " from " << vicCountry->safeGetInt(vicUnitName) << " " << vicUnitName << ".\n";
      Object* air = 0;
      if (airUnitMap[hoiUnitName]) {
	air = theatre->safeGetObject("air");
	if (!air) {
	  air = createObjectWithIdAndType(numUnits++, 41, "air");
	  theatre->setValue(air);
	  air->setLeaf("name", "\"Air Force\""); 
	  air->setLeaf("base", hoiCap);
	  air->setLeaf("location", hoiCap);
	}
      }
      objvec regHolder;
      int divCounter = 1;
      objiter baseVicUnit = vicCountryToUnitsMap[vicCountry][vicUnitName].begin(); 
      for (int i = 0; i < unitsToGenerate; ++i) {
	Object* underlyingVicUnit = (*baseVicUnit);
	++baseVicUnit;
	if (baseVicUnit == vicCountryToUnitsMap[vicCountry][vicUnitName].end()) baseVicUnit = vicCountryToUnitsMap[vicCountry][vicUnitName].begin();
	int numCreated = underlyingVicUnit->safeGetInt("createdHoiUnits");
	++numCreated;
	underlyingVicUnit->resetLeaf("createdHoiUnits", numCreated);
	sprintf(stringbuffer, "%i / %s", numCreated, remQuotes(underlyingVicUnit->safeGetString("name")).c_str());
	Object* regiment = createRegiment(numUnits++, hoiUnitName, stringbuffer, air ? "wing" : "regiment");

	double roll = rand();
	roll /= RAND_MAX;
	if (roll < reserveFraction) {
	  regiment->setLeaf("is_reserve", "yes");
	  double unMobStrength = regiment->safeGetFloat("strength") * reserveFactor;
	  if (!mobilised) regiment->resetLeaf("strength", unMobStrength); 
	}
	
	if (air) {
	  air->setValue(regiment);
	  continue;
	}
	regHolder.push_back(regiment);
	string vicLocation = underlyingVicUnit->safeGetString("location");
	string hoiLocation = selectHoiProvince(vicLocation, hoiCountry);
	
	if (3 <= regHolder.size()) {
	  string name = addQuotes(ordinal(divCounter++) + " " + remQuotes(hoiDivisionNames->safeGetString(hoiUnitName, "Division")));
	  makeHigher(regHolder, numUnits, name, hoiLocation, "division", divHolder);
	}
	if (3 <= divHolder.size()) {
	  string name = addQuotes(ordinal(corCounter++) + " Corps");
	  makeHigher(divHolder, numUnits, name, hoiLocation, "corps", corHolder);
	}
	if (3 <= corHolder.size()) {
	  string name = addQuotes(ordinal(armCounter++) + " Army");
	  makeHigher(corHolder, numUnits, name, hoiLocation, "army", armHolder);
	}
	if (3 <= armHolder.size()) {
	  string name = addQuotes(ordinal(groCounter++) + " Army Group");
	  makeHigher(armHolder, numUnits, name, hoiLocation, "armygroup", groHolder);
	}
      }
      
      while (0 < regHolder.size()) theatre->setValue(pop(regHolder));
    }
    while (0 < divHolder.size()) theatre->setValue(pop(divHolder));
    while (0 < corHolder.size()) theatre->setValue(pop(corHolder));
    while (0 < armHolder.size()) theatre->setValue(pop(armHolder));
    while (0 < groHolder.size()) theatre->setValue(pop(groHolder));
  }

  objvec navalCountries;
  vector<double> weightList;
  double totalNavyWeight = 0; 
  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {
    double shipWeight = (*vc)->safeGetFloat("navy_size");
    double support = (*vc)->safeGetFloat("navy_limit");
    double trueNavySize = 0.5*(shipWeight + support);
    if (shipWeight > support) trueNavySize = support;
    (*vc)->resetLeaf("trueNavySize", trueNavySize);     
    if (trueNavySize < 100) continue;
    totalNavyWeight += trueNavySize;
    navalCountries.push_back(*vc);
    weightList.push_back(trueNavySize);
  }

  map<string, bool> alreadyTried;
  map<string, objvec> countryShipLists;
  for (objiter hoiShip = hoiShipList.begin(); hoiShip != hoiShipList.end(); ++hoiShip) {
    double dieRoll = rand();
    dieRoll /= RAND_MAX;
    dieRoll *= totalNavyWeight;
    double counter = 0;
    for (unsigned int i = 0; i < navalCountries.size(); ++i) {
      counter += weightList[i];
      if (dieRoll > counter) continue;
      setPointersFromVicCountry(navalCountries[i]);
      break;
    }
    if (alreadyTried[hoiTag]) continue;
    Object* navy = hoiCountry->safeGetObject("navy");
    if (!navy) {
      navy = createObjectWithIdAndType(numUnits++, 41, "navy");
      string location = "NotFound";
      for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
	if (remQuotes((*hp)->safeGetString("owner")) != hoiTag) continue;
	if (!(*hp)->safeGetObject("naval_base")) continue;
	location = (*hp)->getKey();
	break; 
      }
      
      if (location == "NotFound") {
	numUnits--;
	Logger::logStream(DebugUnits) << "Could not find naval base for HoI tag " << hoiTag << ", not creating a navy.\n";
	alreadyTried[hoiTag] = true;
	continue; 
      }
      navy->setLeaf("name", vicCountry->safeGetString("largestNavyName", "Conversion navy")); 
      navy->setLeaf("base", location);
      navy->setLeaf("location", location);      
      navy->setLeaf("movement_progress", "0.000");
      navy->setLeaf("is_prioritized", "no");
      navy->setLeaf("can_reinforce", "yes");
      navy->setLeaf("can_upgrade", "yes");
      navy->setLeaf("fuel", "1.000");
      navy->setLeaf("supplies", "1.000");
      hoiCountry->setValue(navy);
    }
    navy->setValue(*hoiShip);
    Object* id = (*hoiShip)->safeGetObject("id");
    id->resetLeaf("id", numUnits++);
    countryShipLists[vicTag].push_back(*hoiShip);
    objvec airforces = (*hoiShip)->getValue("air");
    if (0 == airforces.size()) continue;
    for (objiter airforce = airforces.begin(); airforce != airforces.end(); ++airforce) {
      id = (*airforce)->safeGetObject("id");
      id->resetLeaf("id", numUnits++);
      (*airforce)->unsetValue("leader");
      (*airforce)->resetLeaf("location", navy->safeGetString("location"));
      Object* protection = (*airforce)->safeGetObject("carrier_protection");
      if (protection) protection->resetLeaf("province", navy->safeGetString("location"));
      (*airforce)->unsetValue("path"); 
      objvec wings = (*airforce)->getValue("wing");
      for (objiter wing = wings.begin(); wing != wings.end(); ++wing) {
	id = (*wing)->safeGetObject("id");
	id->resetLeaf("id", numUnits++);
      }
    }
    //break;
  }

  for (map<string, objvec>::iterator i = countryShipLists.begin(); i != countryShipLists.end(); ++i) {
    setPointersFromVicTag((*i).first);
    Logger::logStream(DebugUnits) << "Vic " << vicTag << "(HoI " << hoiTag << ") has navy weight "
				  << vicCountry->safeGetString("trueNavySize") << " and gets ships:\n";
    for (objiter ship = (*i).second.begin(); ship != (*i).second.end(); ++ship) {
      Logger::logStream(DebugUnits) << "  " << (*ship)->safeGetString("name") << "\n"; 
    }
  }
  
  hoiGame->resetLeaf("unit", numUnits); 
  return true; 
}

bool WorkerThread::convertProvinceOwners () {
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    if ((*hp)->safeGetString("owner", NO_OWNER) == NO_OWNER) continue; // Ocean province.
    if (0 == hoiProvToVicProvsMap[*hp].size()) {
      Logger::logStream(Logger::Error) << "Error: No Vic province for HoI province " << (*hp)->getKey() << "\n";
      return false; 
    }
    
    map<string, int> popOwnerMap;
    map<string, int> popControllerMap;    
    string bestOwnerTag = NO_OWNER;
    string bestControllerTag = NO_OWNER;
    set<string> vicCoreTags;
    for (objiter vp = hoiProvToVicProvsMap[*hp].begin(); vp != hoiProvToVicProvsMap[*hp].end(); ++vp) {
      objvec vicCores = (*vp)->getValue("core");
      for (objiter vc = vicCores.begin(); vc != vicCores.end(); ++vc) {
	vicCoreTags.insert(remQuotes((*vc)->getLeaf()));
      }
      
      vicTag = remQuotes((*vp)->safeGetString("owner", NO_OWNER));
      if (vicTag == NO_OWNER) continue;
      int currVicPop = (*vp)->safeGetInt("totalPop");
      if (0 == currVicPop) continue;
      popOwnerMap[vicTag] += currVicPop;
      if (popOwnerMap[vicTag] > popOwnerMap[bestOwnerTag]) bestOwnerTag = vicTag;

      vicTag = remQuotes((*vp)->safeGetString("controller", NO_OWNER));
      popControllerMap[vicTag] += currVicPop;      
      if (popControllerMap[vicTag] > popControllerMap[bestControllerTag]) bestControllerTag = vicTag; 
    }

    objvec inputCores = (*hp)->getValue("core");
    (*hp)->unsetValue("core"); 
    vector<string> inputCoreTags;
    for (set<string>::iterator vct = vicCoreTags.begin(); vct != vicCoreTags.end(); ++vct) {
      setPointersFromHoiTag(vicTagToHoiTagMap[*vct]);
      if (0 == hoiCountryToHoiProvsMap[hoiCountry].size()) continue; // No Vicky revolters. 
      Logger::logStream(DebugCores) << nameAndNumber(*hp) << " is core of Vic " << (*vct) << " and hence HoI " << hoiTag << "\n"; 
      if (find(inputCoreTags.begin(), inputCoreTags.end(), hoiTag) != inputCoreTags.end()) continue;
      (*hp)->setLeaf("core", addQuotes(hoiTag));
      inputCoreTags.push_back(hoiTag); 
    }

    // Put back HoI revolter cores. 
    for (objiter ic = inputCores.begin(); ic != inputCores.end(); ++ic) {
      setPointersFromHoiTag(remQuotes((*ic)->getLeaf())); 
      if (find(inputCoreTags.begin(), inputCoreTags.end(), hoiTag) != inputCoreTags.end()) continue;
      if (0 < hoiCountryToHoiProvsMap[hoiCountry].size()) continue;
      (*hp)->setLeaf("core", addQuotes(hoiTag)); 
    }

    
    if (NO_OWNER == bestOwnerTag) {
      Logger::logStream(Logger::Warning) << "Warning: Unable to assign ownership of HoI province "
					 << (*hp)->getKey() << ", converting from Vic";
      for (objiter vp = hoiProvToVicProvsMap[*hp].begin(); vp != hoiProvToVicProvsMap[*hp].end(); ++vp) {
	Logger::logStream(Logger::Warning) << " " << nameAndNumber(*vp); 
      }
      Logger::logStream(Logger::Warning) << ", skipping.\n";
      continue; 
    }

    setPointersFromVicTag(bestOwnerTag);
    (*hp)->resetLeaf("owner", addQuotes(hoiTag));
    hoiCountryToHoiProvsMap[hoiCountry].push_back(*hp);
    vicCountryToHoiProvsMap[vicCountry].push_back(*hp);
    string hoiOwnerTag = hoiTag;

    setPointersFromVicTag(bestControllerTag);
    string hoiControllerTag = hoiTag;
    (*hp)->resetLeaf("controller", addQuotes(hoiTag));
    Logger::logStream(DebugProvinces) << "HoI province " << (*hp)->getKey()
				      << " owner, controller = Vic "
				      << bestOwnerTag << " -> HoI " << hoiOwnerTag << ", Vic "
				      << bestControllerTag << " -> HoI " << hoiControllerTag 
				      << " based on Vic population " << popOwnerMap[bestOwnerTag]
				      << ", " << popControllerMap[bestControllerTag] << ".\n"; 
  }

  return true; 
}

bool WorkerThread::convertTechs () {
  Logger::logStream(Logger::Game) << "Beginning tech conversion.\n";

  Object* practicalConversion = configObject->safeGetObject("practicals");
  if (!practicalConversion) {
    Logger::logStream(Logger::Error) << "Error: Could not find practicals object in config.txt.\n";
    return false; 
  }
  objvec practicals = practicalConversion->getLeaves();
  map<string, double> maximumTroops;
  map<string, double> maximumPracticals; 
  
  Object* techObject = configObject->getNeededObject("techConversion");
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    Object* hoiTechObject = hoiCountry->safeGetObject("technology");
    if (!hoiTechObject) {
      Logger::logStream(Logger::Warning) << "Warning: No tech object for HoI " << hoiTag << ".\n";
      continue;
    }
    objvec hoiTechList = hoiTechObject->getLeaves();
    for (objiter hoiTech = hoiTechList.begin(); hoiTech != hoiTechList.end(); ++hoiTech) {
      (*hoiTech)->resetToken(0, "0");
    }

    for (objiter practical = practicals.begin(); practical != practicals.end(); ++practical) {
      string key = (*practical)->getKey();
      string exists = hoiCountry->safeGetString(key, "NONE");
      if (exists == "NONE") continue;
      double curr = hoiCountry->safeGetFloat(key);
      if (curr > maximumPracticals[key]) maximumPracticals[key] = curr; 
      hoiCountry->resetLeaf(key, "0.000");
    }
    
    if (!vicCountry) continue;

    for (objiter practical = practicals.begin(); practical != practicals.end(); ++practical) {
      for (int i = 0; i < (*practical)->numTokens(); ++i) {
	string key = (*practical)->getToken(i);
	double currAmount = vicCountry->safeGetInt(key);
	if (currAmount > maximumTroops[key]) maximumTroops[key] = currAmount; 
      }
    }
    
    Object* vicTechObject = vicCountry->safeGetObject("technology");
    if (!vicTechObject) {
      Logger::logStream(Logger::Warning) << "Warning: No tech object for Vic " << vicTag << " (HoI " << hoiTag << ").\n";
      continue;
    }
    objvec vicTechList = vicTechObject->getLeaves();
    Logger::logStream(DebugTechs) << "Vic " << vicTag << " (HoI " << hoiTag << ") tech conversions:\n";
    for (objiter vicTech = vicTechList.begin(); vicTech != vicTechList.end(); ++vicTech) {
      if (0 == (*vicTech)->tokenAsInt(0)) continue;
      string vicTechKey = (*vicTech)->getKey();
      Object* hoiTechKeys = techObject->safeGetObject(vicTechKey); 
      if (!hoiTechKeys) continue;
      for (int i = 0; i < hoiTechKeys->numTokens(); ++i) {
	string hoiTechKey = hoiTechKeys->getToken(i); 
	Object* hoiTech = hoiTechObject->safeGetObject(hoiTechKey);
	if (!hoiTech) {
	  Logger::logStream(Logger::Warning) << "Warning: Ignoring unknown HoI tech " << hoiTechKey << "\n";
	  continue;
	}
	hoiTech->resetToken(0, "1");
	Logger::logStream(DebugTechs) << "  " << vicTechKey << " -> " << hoiTechKey << "\n"; 
      }
    }
  }

  for (objiter hc = hoiCountries.begin(); hc != hoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    for (objiter practical = practicals.begin(); practical != practicals.end(); ++practical) {
      string key = (*practical)->getKey(); 
      for (int i = 0; i < (*practical)->numTokens(); ++i) {
	string troops = (*practical)->getToken(i);
	if (0 == maximumTroops[troops]) continue; 
	double currAmount = vicCountry->safeGetInt(troops);
	currAmount /= maximumTroops[troops];
	currAmount /= (*practical)->numTokens();
	currAmount *= maximumPracticals[key];
	if (0.01 > currAmount) continue;
	hoiCountry->resetLeaf(key, currAmount + hoiCountry->safeGetFloat(key));
	Logger::logStream(DebugTechs) << vicTag << " (HoI " << hoiTag << ") gets " << currAmount << " of practical "
				      << key << " from " << vicCountry->safeGetInt(troops) << " " << troops << ".\n"; 
      }
      // Numbers must have three significant figures or they are read as 1000 times what they actually are. Very strange. 
      double currAmount = hoiCountry->safeGetFloat(key);
      sprintf(stringbuffer, "%.3f", currAmount);
      hoiCountry->resetLeaf(key, stringbuffer); 
    }
  }

  
  Logger::logStream(Logger::Game) << "Done with tech conversion.\n";
  return true;
}

bool WorkerThread::listUrbanProvinces () {
  Logger::logStream(DebugCities) << "Urban provinces:\n";
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    setPointersFromVicProvince(*vp);
    if (!vicCountry) continue;
    double urbanTerrain = (*vp)->safeGetFloat("bureaucrats");
    urbanTerrain       += (*vp)->safeGetFloat("craftsmen");
    urbanTerrain       += (*vp)->safeGetFloat("clerks"); 
  }

  int numUrbanProvinces = configObject->safeGetInt("urbanProvinces", 100);
  int printed = 0;
  
  sort(vicProvinces.begin(), vicProvinces.end(), ObjectDescendingSorter("urbanTerrain"));
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    setPointersFromVicProvince(*vp);
    if (!vicCountry) continue;

    Object* target = 0;
    double bestIndustry = -1;
    for (objiter hp = vicProvToHoiProvsMap[*vp].begin(); hp != vicProvToHoiProvsMap[*vp].end(); ++hp) {
      double currIndustry = 0;
      Object* industry = (*hp)->safeGetObject("industry");
      if (industry) currIndustry = industry->tokenAsFloat(0);
      if (currIndustry < bestIndustry) continue;
      bestIndustry = currIndustry;
      target = (*hp);
    }
    if (!target) continue;
    Logger::logStream(DebugCities) << nameAndNumber(target) << "\n";
    if (++printed >= numUrbanProvinces) break; 
  }
    
  return true;
}

bool WorkerThread::moveCapitals () {
  for (objiter hc = hoiCountries.begin(); hc != hoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    if (0 == hoiCountryToHoiProvsMap[hoiCountry].size()) continue;
    if (hoiTag == "REB") continue;
    string vicCapId = vicCountry->safeGetString("capital", "NONE");
    if (vicCapId == "NONE") {
      Logger::logStream(Logger::Warning) << "Warning: Vic country "
					 << vicTag
					 << " has no capital, skipping HoI "
					 << hoiTag
					 << " capital assignment.\n";
      continue; 
    }
    Object* vicCap = vicProvIdToVicProvMap[vicCapId];
    if (!vicCap) {
      Logger::logStream(Logger::Warning) << "Warning: Could not find province "
					 << vicCapId
					 << ", alleged capital of "
					 << vicTag
					 << ". Skipping HoI "
					 << hoiTag
					 << " capital assignment.\n";
      continue; 
    }

    vicCap->resetLeaf("isCapital", "yes"); 
    Object* hoiCap = selectHoiProvince(vicCap);
    if (!hoiCap) {
      Logger::logStream(Logger::Warning) << "Warning: " << hoiTag
					 << " does not own any provinces matching its Vic capital "
					 << nameAndNumber(vicCap)
					 << ". Assigning capital at random.\n";
      hoiCap = hoiCountryToHoiProvsMap[hoiCountry][0];
    }

    Logger::logStream(Logger::Game) << nameAndNumber(hoiCap) << " is capital of " << hoiTag << ".\n";
    hoiCountry->resetLeaf("capital", hoiCap->getKey());
    hoiCountry->resetLeaf("acting_capital", hoiCap->getKey());
    hoiCap->resetLeaf("capital", "yes"); 
  }
  
  return true; 
}

bool WorkerThread::moveIndustry () {
  double minimumRevenueRate = configObject->safeGetFloat("minimumProfitRate", 0.001);
  double navalBaseWorkers   = configObject->safeGetFloat("navalBaseWorkers", 100); 
  Object* factoryTypes = configObject->getNeededObject("factoryTypes");
  Object* prices = vicGame->getNeededObject("worldmarket");
  prices = prices->getNeededObject("price_pool"); 
  map<string, map<string, bool> > printedWarnings; 

  map<string, bool> warIndustries;
  map<string, bool> heavyIndustries;
  Object* warFactories = configObject->getNeededObject("war_industry");
  for (int i = 0; i < warFactories->numTokens(); ++i) warIndustries[warFactories->getToken(i)] = true;
  warFactories = configObject->getNeededObject("heavy_industry");
  for (int i = 0; i < warFactories->numTokens(); ++i) heavyIndustries[warFactories->getToken(i)] = true;
  double warBonus = configObject->safeGetFloat("warIndustryBonus", 1.2);
  double heavyBonus = configObject->safeGetFloat("heavyIndustryBonus", 1.1);
  
  double totalVicIndustry = 0;
  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {
    setPointersFromVicCountry(*vc); 
    objvec states = vicCountry->getValue("state");
    double throughputMod = vicCountry->safeGetFloat("throughputBonus"); // Ignores inventions. 
    for (objiter state = states.begin(); state != states.end(); ++state) {
      objvec buildings = (*state)->getValue("state_buildings");
      int unemployed = 0;
      int inFactories = 0; 
      double stateWeight = 0;
      double damagedRevenue = 0; 
      // Weight is given by revenue. If less than minimum rate, counts as minimum rate times number of workers.
      // If less than zero, the workers count as unemployed; unemployed workers have the minimum rate, but
      // start 'damaged'.
      
      for (objiter f = buildings.begin(); f != buildings.end(); ++f) {
	if (0 >= (*f)->safeGetInt("level")) continue;
	double revenue = (*f)->safeGetFloat("produces");
	string factoryType = remQuotes((*f)->safeGetString("building", "no-building-keyword"));
	string product = factoryTypes->safeGetString(factoryType, "unknown-product");
	if (0.01 > revenue) {
	  Logger::logStream(DebugIndustry) << "Factory " << factoryType << " owned by " << vicTag << " in state " << (*state)->safeGetObject("id")->safeGetString("id")
					   << " produces " << revenue << " " << product << ".\n"; 
	}	
	double price = prices->safeGetFloat(product, -1);
	if (0 > price) {
	  if (!printedWarnings[factoryType][product]) {
	    Logger::logStream(Logger::Warning) << "No price found for factory type " << factoryType
					       << " and product " << product << ", using default 1.\n";
	    printedWarnings[factoryType][product] = true; 
	  }
	  price = 1; 
	}
	revenue *= price;
	
	double throughput = (*f)->safeGetInt("craftsmen") * 0.0001;
	throughput /= (*f)->safeGetInt("level");
	double modifiedThroughput = throughput * (1 + throughputMod - 0.01 * vicCountry->safeGetFloat("war_exhaustion"));
	throughput *= (1 + throughputMod);
	Object* employment = (*f)->safeGetObject("employment");
	if (!employment) continue;
	employment = employment->safeGetObject("employees");
	if (!employment) continue;
	objvec employees = employment->getLeaves();
	int workers = 0; 
	for (objiter emp = employees.begin(); emp != employees.end(); ++emp) {
	  workers += (*emp)->safeGetInt("count");
	}
	double revenuePerWorker = revenue / workers;
	Logger::logStream(DebugIndustry) << "Revenue per worker in "
					 << factoryType << " in state "
					 << (*state)->safeGetObject("id")->safeGetString("id")
					 << " is " << revenuePerWorker << ".\n"; 
	if (revenuePerWorker < minimumRevenueRate) revenuePerWorker = minimumRevenueRate;
	if (warIndustries[factoryType]) revenuePerWorker *= warBonus;
	else if (heavyIndustries[factoryType]) revenuePerWorker *= heavyBonus; 
	if (0 > revenue) unemployed += workers;
	else stateWeight += revenuePerWorker * workers;
	inFactories += workers;
	if (0 < modifiedThroughput) damagedRevenue += revenue * (throughput / modifiedThroughput - 1);
      }
      Object* provs = (*state)->getNeededObject("provinces");
      int totalWorkers = 0;
      Object* hoiProv = 0;
      bool foundName = false;
      int bestSize = 0;
      double navalBaseBonus = 0; 
      for (int i = 0; i < provs->numTokens(); ++i) {
	Object* vicProv = vicProvIdToVicProvMap[provs->getToken(i)];
	if (!vicProv) continue;
	int currWorkers = 0; 
	objvec craftsmen = vicProv->getValue("craftsmen");
	for (objiter pop = craftsmen.begin(); pop != craftsmen.end(); ++pop) currWorkers += (*pop)->safeGetInt("size");
	craftsmen = vicProv->getValue("clerks");
	for (objiter pop = craftsmen.begin(); pop != craftsmen.end(); ++pop) currWorkers += (*pop)->safeGetInt("size");
	totalWorkers += currWorkers;
	navalBaseBonus += calcForceLimit(vicProv->safeGetObject("naval_base")) * minimumRevenueRate * navalBaseWorkers;
	if (foundName) continue;
	Object* hoiCand = selectHoiProvince(vicProv);
	if (!hoiCand) continue; 
	if (hoiCand->safeGetString("name") == vicProv->safeGetString("name")) {
	  hoiProv = hoiCand;
	  foundName = true;
	  continue;
	}
	if (currWorkers < bestSize) continue;
	bestSize = currWorkers;
	hoiProv = hoiCand; 
      }
      if (!hoiProv) continue; 
      unemployed += (totalWorkers - inFactories);
      unemployed *= minimumRevenueRate;
      stateWeight += damagedRevenue;
      stateWeight += unemployed;
      stateWeight += navalBaseBonus; 
      totalVicIndustry += stateWeight;
      hoiProv->resetLeaf("vicIndustry", stateWeight + hoiProv->safeGetFloat("vicIndustry"));
      hoiProv->resetLeaf("vicUnemployed", unemployed + damagedRevenue + hoiProv->safeGetFloat("vicUnemployed")); 
    }
  }

  double totalHoiIndustry = 0;
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    Object* ind = (*hp)->safeGetObject("industry");
    if (!ind) continue; 
    totalHoiIndustry += ind->tokenAsFloat(0);
    (*hp)->unsetValue("industry");
  }

  double hoiICperVicIndustry = totalHoiIndustry / totalVicIndustry;
  Logger::logStream(DebugIndustry) << hoiICperVicIndustry << " " << totalHoiIndustry << " " << totalVicIndustry << "\n"; 
  map<string, pair<double, double> > tagToIcMap; 
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    double vicInd = (*hp)->safeGetFloat("vicIndustry", -1);
    if (0 > vicInd) continue;
    double hoiMaxInd = vicInd * hoiICperVicIndustry;
    if (0.01 > hoiMaxInd) continue;
    Object* industry = new Object("industry");
    (*hp)->setValue(industry);
    vicInd -= (*hp)->safeGetFloat("vicUnemployed");
    double hoiCurrInd = vicInd * hoiICperVicIndustry;
    sprintf(stringbuffer, "%.2f", hoiCurrInd);
    industry->addToList(stringbuffer);
    sprintf(stringbuffer, "%.2f", hoiMaxInd);
    industry->addToList(stringbuffer);
    tagToIcMap[(*hp)->safeGetString("owner")].first += hoiCurrInd;
    tagToIcMap[(*hp)->safeGetString("owner")].second += hoiMaxInd;
  }

  Logger::logStream(Logger::Game) << "Industry totals: \n";
  for (map<string, pair<double, double> >::iterator hc = tagToIcMap.begin(); hc != tagToIcMap.end(); ++hc) {
    Logger::logStream(Logger::Game) << (*hc).first << ": " << (*hc).second.first << " / " << (*hc).second.second << "\n"; 
  }

  return true; 
}

bool WorkerThread::moveResources () {
  // Check that we have needed information.
  vector<string> objects;
  objects.push_back("fightingClasses");
  objects.push_back("officerClasses");
  for (vector<string>::iterator o = objects.begin(); o != objects.end(); ++o) {
    Object* fightingClasses = configObject->safeGetObject(*o);
    if (!fightingClasses) {
      Logger::logStream(Logger::Error) << "Error: No " << (*o) << " object in config.txt.\n";
      return false;
    }
    objvec classes = fightingClasses->getValue("class");
    if (0 == classes.size()) {
      Logger::logStream(Logger::Error) << "Error: " << (*o) << " object in config.txt is empty - should include at least one POP type.\n"; 
      return false;
    }
  }
  
  map<string, double> hoiWorldTotals;
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    hoiWorldTotals["manpower"]       += (*hp)->safeGetFloat("manpower");
    hoiWorldTotals["leadership"]     += (*hp)->safeGetFloat("leadership");
    (*hp)->unsetValue("manpower");
    (*hp)->unsetValue("leadership");
    Object* production = (*hp)->safeGetObject("max_producing");
    if (!production) continue;
    hoiWorldTotals["energy"]         += production->safeGetFloat("energy");
    hoiWorldTotals["metal"]          += production->safeGetFloat("metal");
    hoiWorldTotals["crude_oil"]      += production->safeGetFloat("crude_oil");
    hoiWorldTotals["rare_materials"] += production->safeGetFloat("rare_materials");
    production->unsetValue("energy");
    production->unsetValue("metal");
    production->unsetValue("crude_oil");
    production->unsetValue("rare_materials");
  }

  map<string, double> vicWorldTotals;
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    for (map<string, double>::iterator r = hoiWorldTotals.begin(); r != hoiWorldTotals.end(); ++r) {
      string resName = (*r).first; 
      vicWorldTotals[resName]       += calculateVicProduction((*vp), resName);
    }
  }

  Logger::logStream(DebugResources) << "Resource totals:\n"; 
  for (map<string, double>::iterator r = hoiWorldTotals.begin(); r != hoiWorldTotals.end(); ++r) {
    string resName = (*r).first;
    Logger::logStream(DebugResources) << "  " << resName << " Vic: " << vicWorldTotals[resName] << " HoI: " << (*r).second << "\n"; 
  }
  
  map<string, map<string, double> > overflows;
  map<string, map<string, double> > countryTotals; 
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    string vicTag = (*vp)->safeGetString("owner", NO_OWNER);
    if (NO_OWNER == vicTag) continue;
    setPointersFromVicProvince(*vp); 
    Object* hoiProv = selectHoiProvince(*vp);
    for (map<string, double>::iterator r = hoiWorldTotals.begin(); r != hoiWorldTotals.end(); ++r) {
      string resName = (*r).first;
      double curr = calculateVicProduction((*vp), resName);
      if (0 >= curr) continue;
      curr /= vicWorldTotals[resName];
      curr *= hoiWorldTotals[resName]; 
      
      if (!hoiProv) {
	overflows[vicTag][resName] += curr;
	continue;
      }
      Object* target = 0;
      if ((resName == "manpower") || (resName == "leadership")) target = hoiProv;
      else target = hoiProv->getNeededObject("max_producing");
      
      curr += overflows[vicTag][resName];
      curr += target->safeGetFloat(resName);
      overflows[vicTag][resName] = curr - floor(curr);
      curr = floor(curr);
      if (0.5 > curr) continue; 

      target->resetLeaf(resName, curr);
      countryTotals[vicTag][resName] += curr; 
    }
  }

  Logger::logStream(DebugResources) << "Country totals:\n"; 
  for (map<string, map<string, double> >::iterator v = countryTotals.begin(); v != countryTotals.end(); ++v) {
    vicTag = (*v).first;
    Logger::logStream(DebugResources) << vicTag << ":\n";
    for (map<string, double>::iterator r = (*v).second.begin(); r != (*v).second.end(); ++r) {
      string resName = (*r).first;
      Logger::logStream(DebugResources) << "  " << resName << ": " << (*r).second << "\n"; 
    }
  }
  
  
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    Object* production = (*hp)->safeGetObject("max_producing");
    if ((!production) || (0 == production->getLeaves().size())) {
      (*hp)->unsetValue("max_producing");
      (*hp)->unsetValue("current_producing"); 
      continue;
    }
    Object* curr = (*hp)->getNeededObject("current_producing");
    curr->clear();
    objvec leaves = production->getLeaves();
    for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
      curr->setValue(*leaf); 
    }
  }  
  

  return true;
}

bool WorkerThread::moveStockpiles () {
  Logger::logStream(Logger::Game) << "Starting stockpiles.\n";
  Object* piles = configObject->getNeededObject("stockpiles");
  objvec stocks = piles->getLeaves();
  for (objiter stock = stocks.begin(); stock != stocks.end(); ++stock) {
    if (4 <= (*stock)->numTokens()) continue;
    Logger::logStream(Logger::Error) << "Error: Stockpile entry " << (*stock) << " should have at least four tokens.\n";
    return false; 
  }
  map<string, double> totalHoiStuff;
  map<string, double> totalVicStuff;
  for (objiter hc = allHoiCountries.begin(); hc != allHoiCountries.end(); ++hc) {
    setPointersFromHoiCountry(*hc);
    for (objiter stock = stocks.begin(); stock != stocks.end(); ++stock) {
      string hoiKey = (*stock)->getKey();

      Object* target = hoiCountry;
      if ((*stock)->getToken(0) != "country") target = hoiCountry->getNeededObject((*stock)->getToken(0));
      double hoiAmount = target->safeGetFloat(hoiKey);
      //Logger::logStream(DebugStockpiles) << hoiTag << " " << hoiKey << ": " << hoiAmount << "\n"; 
      totalHoiStuff[hoiKey] += hoiAmount;
      target->resetLeaf(hoiKey, "0.000");
      if (!vicCountry) continue;
      if (0 == vicCountryToHoiProvsMap[vicCountry].size()) continue;
      target = vicCountry;
      if ((*stock)->getToken(1) != "country") target = vicCountry->getNeededObject((*stock)->getToken(1));
      for (int i = 3; i < (*stock)->numTokens(); ++i) {
	string vicKey = (*stock)->getToken(i);
	totalVicStuff[vicKey] += target->safeGetFloat(vicKey);
      }
    }
  }

  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {
    setPointersFromVicCountry(*vc);
    if (!hoiCountry) continue;
    if (0 == vicCountryToHoiProvsMap[vicCountry].size()) continue;
    Logger::logStream(DebugStockpiles) << "Stockpiles for " << vicTag << " (HoI " << hoiTag << "):\n";    
    for (objiter stock = stocks.begin(); stock != stocks.end(); ++stock) {
      string hoiKey = (*stock)->getKey();
      Object* hoiTarget = hoiCountry;
      if ((*stock)->getToken(0) == "cap_pool") {
	hoiTarget = hoiProvIdToHoiProvMap[hoiCountry->safeGetString("capital", "BLAH")];
	if (!hoiTarget) continue;
	hoiTarget = hoiTarget->getNeededObject("pool");
      }
      else if ((*stock)->getToken(0) != "country") hoiTarget = hoiCountry->getNeededObject((*stock)->getToken(0));
      Object* vicTarget = vicCountry;
      if ((*stock)->getToken(1) != "country") vicTarget = vicCountry->getNeededObject((*stock)->getToken(1));
      double vicAmount = 0;
      double vicTotal = 1; 
      for (int i = 3; i < (*stock)->numTokens(); ++i) {
	string vicKey = (*stock)->getToken(i);
	vicAmount += vicTarget->safeGetFloat(vicKey);
	vicTotal += totalVicStuff[vicKey]; 
      }
      int hoiAmount = (int) floor(0.5 + vicAmount * totalHoiStuff[hoiKey] / vicTotal); 
      hoiAmount += (*stock)->tokenAsFloat(2); 
      hoiTarget->resetLeaf(hoiKey, hoiAmount);
      Logger::logStream(DebugStockpiles) << "  " << hoiKey << ": " << hoiAmount << "\n";
    }
  }

  
  Logger::logStream(Logger::Game) << "Done with stockpiles.\n";
  return true;
}

double getWeight (Object* algo, string approach, Object* vicProv) {
  if (vicProv->safeGetString("has_sr", "no") == "yes") return 0; 
  
  if ((approach == "random") || (!algo) || (algo->safeGetString("type") == "random")) {
    return rand(); 
  }

  return rand(); 
}

bool WorkerThread::moveStrategicResources () {
  Logger::logStream(Logger::Game) << "Starting strategic resources.\n";
  string approach = configObject->safeGetString("strategicResources", "remove");
  if (approach == "historical") {
    Logger::logStream(Logger::Game) << "Leaving in place.\n";
    return true;
  }

  map<string, Object*> algoMap;
  map<string, unsigned int> resourceMap; 
  Object* stratObject = configObject->getNeededObject("srConversion");
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    string resource = (*hp)->safeGetString("strategic_resource", "none");
    if (resource == "none") continue;
    resourceMap[resource]++; 
    (*hp)->unsetValue("strategic_resource");
    if (algoMap[resource]) continue;
    algoMap[resource] = stratObject->safeGetObject(resource); 
  }

  if (approach == "remove") {
    Logger::logStream(Logger::Game) << "Removed.\n";
    return true;
  }

  if ((approach != "random") && (approach != "redistribute")) {
    Logger::logStream(Logger::Game) << "Don't know what to do with approach \"" << approach << "\", defaulting to remove.\n";
    return true;
  }

  for (map<string, Object*>::iterator r = algoMap.begin(); r != algoMap.end(); ++r) {
    if (((*r).second) && ((*r).second->safeGetString("type") == "remove")) {
      Logger::logStream(DebugStratResources) << "Removing " << (*r).first << ".\n"; 
      continue;
    }
    for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
      (*vp)->resetLeaf("srWeight", getWeight((*r).second, approach, (*vp))); 
    }

    sort(vicProvinces.begin(), vicProvinces.end(), ObjectDescendingSorter("srWeight"));
    int vicProvPointer = -1; 
    for (unsigned int i = 0; i < resourceMap[(*r).first]; ++i) {
      while (true) {
	vicProvPointer++; 
	Object* vicProv = vicProvinces[vicProvPointer];
	setPointersFromVicProvince(vicProv);
	if (!vicCountry) continue;
	Object* hoiProv = 0;
	for (objiter hp = vicProvToHoiProvsMap[vicProv].begin(); hp != vicProvToHoiProvsMap[vicProv].end(); ++hp) {
	  if (hoiTag != remQuotes((*hp)->safeGetString("owner"))) continue;
	  if ((*hp)->safeGetString("strategic_resource", "none") != "none") continue;
	  hoiProv = (*hp);
	  break;
	}
	if (!hoiProv) continue;
	Logger::logStream(DebugStratResources) << "Putting " << (*r).first << " in " << nameAndNumber(hoiProv)
					     << " from " << nameAndNumber(vicProv) << " with weight " << vicProv->safeGetString("srWeight") << "\n";
	hoiProv->resetLeaf("strategic_resource", (*r).first);
	vicProv->resetLeaf("has_sr", "yes");
	break; 
      }
    }
  }
  
  Logger::logStream(Logger::Game) << "Done redistributing strategic resources.\n";
  return true;
}

/******************************* End conversions ********************************/

/*******************************  Begin calculators ********************************/

void addCoords (int& numLeaves, double& xpos, double& ypos, Object* entry, string secondName = "") {
  if (!entry) return;
  if (secondName == "") {
    numLeaves++; 
    xpos += entry->safeGetFloat("x");
    ypos += entry->safeGetFloat("y");
  }
  else addCoords(numLeaves, xpos, ypos, entry->safeGetObject(secondName));
}

void calculateCentroid (Object* province, bool vic) {
  objvec leaves = province->getLeaves();
  double xpos = 0;
  double ypos = 0;
  static vector<string> vicLeafNames;
  static vector<string> vicSecNames;
  static vector<string> hoiSecNames; 
  if (0 == vicLeafNames.size()) {
    //vicLeafNames.push_back("unit"); // This would include ocean provinces. 
    vicLeafNames.push_back("building_construction");
    vicLeafNames.push_back("military_construction");
    vicLeafNames.push_back("factory");

    vicSecNames.push_back("fort");
    vicSecNames.push_back("naval_base");
    vicSecNames.push_back("railroad");

    hoiSecNames.push_back("air_base");
    hoiSecNames.push_back("naval_base");
    hoiSecNames.push_back("coastal_fort");
    hoiSecNames.push_back("land_fort");
    hoiSecNames.push_back("anti_air");
  }

  int numLeaves = 0;

  vector<string>* leafNames = &vicLeafNames;
  for (vector<string>::iterator s = leafNames->begin(); s != leafNames->end(); ++s) {
    if (!vic) break;
    addCoords(numLeaves, xpos, ypos, province->safeGetObject(*s));
  }

  Object* bpos = province->safeGetObject("building_position");
  leafNames = vic ? &vicSecNames : &hoiSecNames; 
  for (vector<string>::iterator s = leafNames->begin(); s != leafNames->end(); ++s) {
    addCoords(numLeaves, xpos, ypos, bpos, (*s));
  }
  
  if (0 == numLeaves) {
    province->resetLeaf("ocean", "yes");
    return;
  }

  xpos /= numLeaves;
  ypos /= numLeaves;
  province->resetLeaf("xpos", xpos);
  province->resetLeaf("ypos", ypos);  
}

double calculateDistance (Object* one, Object* two) {
  double xdist = one->safeGetFloat("xpos") - two->safeGetFloat("xpos");
  double ydist = one->safeGetFloat("ypos") - two->safeGetFloat("ypos");  
  return sqrt(xdist*xdist + ydist*ydist); 
}

double calcAvg (Object* ofthis) {
  if (!ofthis) return 0; 
  int num = ofthis->numTokens();
  if (0 == num) return 0;
  double ret = 0;
  for (int i = 0; i < num; ++i) {
    ret += ofthis->tokenAsFloat(i);
  }
  ret /= num;
  return ret; 
}

double WorkerThread::calculateGovResemblance (Object* vicCountry, Object* hoiCountry) {
  double ret = rand();
  ret /= RAND_MAX;
  ret *= 0.001;
  static Object* resemblances = configObject->safeGetObject("govResemblance");
  if (!resemblances) {
    static bool printed = false;
    if (!printed) {
      printed = true;
      Logger::logStream(Logger::Error) << "Warning: Could not find govResemblance object in config.\n";
    }
    return ret;
  }

  Object* resemblance = resemblances->safeGetObject(hoiCountry->getKey());
  if (!resemblance) {
    static map<string, bool> printed;
    if (!printed[hoiCountry->getKey()]) {
      Logger::logStream(DebugGovernments) << "No resemblance object found for " << hoiCountry->getKey() << "\n";
      printed[hoiCountry->getKey()] = true;
    }
    return ret; 
  }

  static double humanFactor = resemblances->safeGetFloat("humanFactor", 0.1);
  if (vicCountry->safeGetString("human", "no") == "yes") ret += humanFactor; 
  
  objvec leaves = resemblance->getLeaves();
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    Object* target = vicCountry;
    string keyword = (*leaf)->getKey();
    string targetKey = (*leaf)->safeGetString("target", "none");
    if (targetKey != "none") target = target->safeGetObject(targetKey);
    
    double amount = 0;
    string value = target->safeGetString(keyword);    
    if ((*leaf)->safeGetString("numerical", "no") == "yes") {
      amount = target->safeGetFloat(keyword) * (*leaf)->safeGetFloat("value"); 
    }
    else {
      amount = (*leaf)->safeGetFloat(value, amount);
    }
    if (fabs(amount) > 0.001) {
      Logger::logStream(DebugGovernments) << "Vic country "
					  << vicTag
					  << " gets "
					  << amount
					  << " resemblance points to "
					  << hoiCountry->getKey()
					  << " from "
					  << keyword << " " << value
					  << "\n";
      ret += amount; 
    }
  }
  ret *= resemblance->safeGetFloat("scale", 1); 
  return ret;
}

double WorkerThread::calculateVicProduction (Object* vicProvince, string resource) {
  double cached = vicProvince->safeGetFloat(resource, -1);
  if (0 <= cached) return cached;

  double ret = 0;  
  Object* rgo = vicProvince->safeGetObject("rgo");
  Object* resourceConversion = configObject->safeGetObject(resource);
  if ((rgo) && (resourceConversion)) {
    string goods = remQuotes(rgo->safeGetString("goods_type"));
    ret = rgo->safeGetFloat("last_income") * resourceConversion->safeGetFloat(goods, -1);
    if (ret >= 0) {
      vicProvince->setLeaf(resource, ret);
      return ret;
    }
  }

  if (resource == "manpower") {
    static Object* fightingClasses = configObject->getNeededObject("fightingClasses");
    static objvec classes = fightingClasses->getValue("class");
    for (objiter poptype = classes.begin(); poptype != classes.end(); ++poptype) {
      objvec pops = vicProvince->getValue((*poptype)->getLeaf());
      for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
	ret += (*pop)->safeGetFloat("size");
      }
    }
    vicProvince->setLeaf(resource, ret);
    return ret; 
  }

  if (resource == "leadership") {
    static Object* officerClasses = configObject->getNeededObject("officerClasses");
    static objvec classes = officerClasses->getValue("class");
    for (objiter poptype = classes.begin(); poptype != classes.end(); ++poptype) {
      objvec pops = vicProvince->getValue((*poptype)->getLeaf());
      for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
	ret += (*pop)->safeGetFloat("size");
      }
    }
    vicProvince->setLeaf(resource, ret);
    return ret; 
  }

  vicProvince->setLeaf(resource, 0);
  return 0; 
}

double WorkerThread::extractStrength (Object* unit, Object* reserves) {
  double ret = 0; 
  objvec regiments = unit->getLeaves();
  for (objiter regiment = regiments.begin(); regiment != regiments.end(); ++regiment) {
    if ((*regiment)->isLeaf()) continue;
    if ((*regiment)->getKey() == "regiment") {
      double strength = (*regiment)->safeGetFloat("strength");
      ret += strength;
      string regType = remQuotes((*regiment)->safeGetString("type"));
      hoiUnitTypes[regType]++;
      if ((*regiment)->safeGetString("is_reserve", "no") == "yes") reserves->resetLeaf(regType, 1 + reserves->safeGetInt(regType));
    }
    else ret += extractStrength(*regiment, reserves);
  }
  return ret; 
}

/******************************* End calculators ********************************/


void WorkerThread::autoMap () {
  hoiGame = new Object("province_mapping");
  Object* vpObject = loadTextFile(sourceVersion + "positions.txt");
  Object* hpObject = loadTextFile(targetVersion + "positions.txt");
  Object* hrObject = loadTextFile(targetVersion + "region.txt");

  objvec vicProvinces = vpObject->getLeaves();
  objvec hoiProvinces = hpObject->getLeaves();
  objvec hoiRegions   = hrObject->getLeaves();

  map<string, Object*> vicNameToProvMap;
  map<int, Object*> vicNumberToProvMap;
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    string name = (*vp)->safeGetString("name", "NONAME");
    if (name == "NONAME") {
      Logger::logStream(Logger::Warning) << "Warning: V2 province position without name, skipping: " << (*vp) << "\n";
      continue;
    }
    vicNameToProvMap[name] = (*vp);
    vicNumberToProvMap[atoi((*vp)->getKey().c_str())] = (*vp);
    calculateCentroid(*vp, true); 
  }

  map<string, Object*> hoiNameToProvMap;
  map<int, Object*> hoiNumberToProvMap;
  vector<pair<Object*, Object*> > fixedPoints; 
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    string name = (*hp)->safeGetString("name", "NONAME");
    if (name == "NONAME") {
      Logger::logStream(Logger::Warning) << "Warning: HoI province position without name, skipping: " << (*hp) << "\n";
      continue;
    }
    hoiNameToProvMap[name] = (*hp);
    hoiNumberToProvMap[atoi((*hp)->getKey().c_str())] = (*hp);
    calculateCentroid(*hp, false);
    if ((*hp)->safeGetString("ocean", "no") == "yes") continue; 
    
    Object* vp = vicNameToProvMap[name];
    if (!vp) continue;
   
    Logger::logStream(Logger::Game) << "Linking provinces " << name << ", HoI "
				    << (*hp)->getKey() << " <-> Vic " << vp->getKey() << "\n";

    fixedPoints.push_back(pair<Object*, Object*>(vp, (*hp)));
    hoiProvToVicProvsMap[*hp].push_back(vp);
    vicProvToHoiProvsMap[vp].push_back(*hp);
  }

  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    if (0 == hoiProvToVicProvsMap[*hp].size()) continue;
    if ((*hp)->safeGetString("ocean", "no") == "yes") continue; 

    // Find two closest fixed points
    pair<Object*, Object*> closest = fixedPoints[0];
    pair<Object*, Object*> almost  = fixedPoints[1];
    double hoiLeast = calculateDistance((*hp), closest.second);
    double hoiNext  = calculateDistance((*hp), almost.second);
    if (hoiLeast > hoiNext) {
      closest = fixedPoints[1];
      almost  = fixedPoints[0];
      double temp = hoiLeast;
      hoiLeast = hoiNext;
      hoiNext = temp; 
    }

    for (unsigned int fix = 2; fix < fixedPoints.size(); ++fix) {
      double hoiCurr = calculateDistance((*hp), fixedPoints[fix].second);
      if (hoiCurr >= hoiNext) continue;
      almost = fixedPoints[fix];
      hoiNext = hoiCurr;
      if (hoiCurr >= hoiLeast) continue;
      almost = closest;
      hoiNext = hoiLeast;
      closest = fixedPoints[fix];
      hoiLeast = hoiCurr; 
    }

    Logger::logStream(Logger::Game) << "Finding link for HoI " << (*hp)->safeGetString("name")
				    << ". Closest are HoI " << closest.second->safeGetString("name")
				    << " (" << closest.second->safeGetString("xpos") << ", " << closest.second->safeGetString("ypos")
				    << ") and "
				    << almost.second->safeGetString("name")
				    << " (" << almost.second->safeGetString("xpos") << ", " << almost.second->safeGetString("ypos")
				    << ").\n";
    
    double hoiXDistance = (*hp)->safeGetFloat("xpos") - closest.second->safeGetFloat("xpos");
    double hoiYDistance = (*hp)->safeGetFloat("ypos") - closest.second->safeGetFloat("ypos");

    double hoiScaleX = almost.second->safeGetFloat("xpos") - closest.second->safeGetFloat("xpos");
    double hoiScaleY = almost.second->safeGetFloat("ypos") - closest.second->safeGetFloat("ypos");    
    double vicScaleX = almost.first->safeGetFloat("xpos") - closest.first->safeGetFloat("xpos");
    double vicScaleY = almost.first->safeGetFloat("ypos") - closest.first->safeGetFloat("ypos");

    double scaleX = vicScaleX / hoiScaleX;
    double scaleY = vicScaleY / hoiScaleY;

    double vicXDistance = hoiXDistance * scaleX;
    double vicYDistance = hoiYDistance * scaleY;

    static Object* vicDummyPos = new Object("dummypos");
    vicDummyPos->resetLeaf("xpos", closest.first->safeGetFloat("xpos") + vicXDistance);
    vicDummyPos->resetLeaf("ypos", closest.first->safeGetFloat("ypos") + vicYDistance);

    Object* vicBest = 0;
    double vicLeast = 1e25;
    for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
      if ((*vp)->safeGetString("ocean", "no") == "yes") continue;
      double vicCurr = calculateDistance((*vp), vicDummyPos);
      if (vicCurr >= vicLeast) continue;
      vicLeast = vicCurr;
      vicBest = (*vp);
    }

    Logger::logStream(Logger::Game) << "Closest Vic positions: " << closest.first->safeGetString("name")
				    << " (" << closest.first->safeGetString("xpos") << ", " << closest.first->safeGetString("ypos")
				    << ") and "
				    << almost.first->safeGetString("name")
				    << " (" << almost.first->safeGetString("xpos") << ", " << almost.first->safeGetString("ypos")
				    << ") giving HoI distance (" << hoiXDistance << ", " << hoiYDistance 
				    << ") Vic distance (" << vicXDistance << ", " << vicYDistance
				    << ") scaling (" << scaleX << ", " << scaleY
				    << ") dummyPos " << vicDummyPos->safeGetString("xpos") << ", " << vicDummyPos->safeGetString("ypos")
				    << ") bestVic (" << vicBest->safeGetString("xpos") << ", " << vicBest->safeGetString("ypos")
				    << ")\n"; 
      
    hoiProvToVicProvsMap[*hp].push_back(vicBest);
    vicProvToHoiProvsMap[vicBest].push_back(*hp); 
    Logger::logStream(Logger::Game) << "Linking HoI " << nameAndNumber(*hp) << " <-> Vic " << nameAndNumber(vicBest) << "\n"; 
  }

  for (map<Object*, objvec>::iterator link = vicProvToHoiProvsMap.begin(); link != vicProvToHoiProvsMap.end(); ++link) {
    Object* l = new Object("link");
    hoiGame->setValue(l);
    l->setLeaf("vic", (*link).first->getKey());
    sprintf(stringbuffer, "Vic %s to HoI ", (*link).first->safeGetString("name").c_str());
    for (objiter h = (*link).second.begin(); h != (*link).second.end(); ++h) {
      l->setLeaf("hoi", (*h)->getKey());
      sprintf(stringbuffer, "%s %s", stringbuffer, (*h)->safeGetString("name").c_str()); 
    }
    l->setComment(stringbuffer); 
  }

  
  Logger::logStream(Logger::Game) << "Done with generating, writing to Output/province_mappings.txt.\n";
  ofstream writer;
  writer.open(".\\Output\\province_mappings.txt");
  Parser::topLevel = hoiGame;
  writer << (*hoiGame);
  writer.close();
  Logger::logStream(Logger::Game) << "Done writing.\n";
}

void WorkerThread::getStatistics () {
  if (!vicGame) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  Logger::logStream(Logger::Game) << "Done with statistics.\n";
}
 
void WorkerThread::convert () {
  if (!vicGame) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  Logger::logStream(Logger::Game) << "Loading HoI source file.\n";
  hoiGame = loadTextFile(targetVersion + "input.hoi3");

  loadFiles();
  initialiseHoiSummaries();
  if (!createProvinceMap()) return; 
  if (!createCountryMap()) return;
  initialiseVicSummaries();     
  if (!convertProvinceOwners()) return;
  if (!moveCapitals()) return; // Must precede OOBs or the capitals won't be placed right.
  if (!convertBuildings()) return; // Must precede OOBs or there won't be naval bases. Must be after ownership conversion. Must be after capitals for urbanity.
  if (!convertLaws()) return; // Must be before OOBs or mob laws won't be set.   
  if (!convertOoBs()) return; 
  if (!moveResources()) return;
  if (!moveIndustry()) return;
  if (!convertGovernments()) return; 
  if (!convertDiplomacy()) return; // Sets casualties for Vicky nations. 
  if (!convertLeaders()) return; // Must come after OOBs or true navy size won't be set. 
  if (!convertTechs()) return;
  if (!moveStockpiles()) return;
  if (!convertMisc()) return; // Uses casualties, must be after diplomacy.
  if (!listUrbanProvinces()) return;
  if (!moveStrategicResources()) return; 
  cleanUp(); 
  
  Logger::logStream(Logger::Game) << "Done with conversion, writing to Output/converted.hoi3.\n";
 
  ofstream writer;
  writer.open(".\\Output\\converted.hoi3");
  Parser::topLevel = hoiGame;
  writer << (*hoiGame);
  writer.close();
  Logger::logStream(Logger::Game) << "Done writing.\n";
}
