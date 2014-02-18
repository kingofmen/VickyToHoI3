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

/* TODOs
 * Resources
 * IC
 * Orders of battle
 * Governments
 * Techs
 * Officers
 * Buildings 
 */

char stringbuffer[10000];
CleanerWindow* parentWindow;
ofstream* debugFile = 0; 

Object* hoiCountry = 0;
Object* vicCountry = 0;
string hoiTag;
string vicTag; 

const string NO_OWNER = "\"NONE\""; 

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
  : targetVersion(".\\HoI_Vanilla\\")
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
  hoiTagToHoiCountryMap[hoiTag] = hoiCountry; 
  Logger::logStream(Logger::Game) << "Assigning Vic country " << vicTag << " <-> HoI " << hoiTag << "\n"; 
}

void WorkerThread::cleanUp () {
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    (*hp)->unsetValue("name"); 
  }
}

void WorkerThread::configure () {
  configObject = processFile("config.txt");
  targetVersion = configObject->safeGetString("hoidir", ".\\HoI_Vanilla\\");
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
  vicTag = vicCountry->getKey();
}

void WorkerThread::setPointersFromVicProvince (Object* vp) {
  vicTag = remQuotes(vp->safeGetString("owner", NO_OWNER));
  hoiTag = vicTagToHoiTagMap[vicTag];
  hoiCountry = hoiTagToHoiCountryMap[hoiTag];
  vicCountry = hoiCountryToVicCountryMap[hoiCountry];
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
    if ((*leaf)->safeGetString("owner", "NONE") == "NONE") continue;
    hoiProvinces.push_back(*leaf);
    (*leaf)->resetLeaf("name", provinceNamesObject->safeGetString((*leaf)->getKey(), "NO_NAME"));
    (*leaf)->resetLeaf("capital", "no"); 
    if (0 < hoiProvToVicProvsMap[*leaf].size()) continue; 
    Logger::logStream(Logger::Warning) << "Warning: Hoi province " << (*leaf)->getKey() << " has no assigned Vicky province.\n"; 
  }

  return true; 
}

void WorkerThread::initialiseVicSummaries () {
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    objvec leaves = (*vp)->getLeaves();
    int totalPop = 0;
    for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
      totalPop += (*leaf)->safeGetInt("size");
    }
    (*vp)->resetLeaf("totalPop", totalPop); 
  }
}

void WorkerThread::loadFiles () {
  provinceMapObject   = loadTextFile(sourceVersion + "province_mappings.txt");
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

    (*hp)->unsetValue("core");
    for (set<string>::iterator vct = vicCoreTags.begin(); vct != vicCoreTags.end(); ++vct) {
      string hoiCoreTag = vicTagToHoiTagMap[*vct];
      (*hp)->setLeaf("core", addQuotes(hoiCoreTag));
      Logger::logStream(Logger::Game) << nameAndNumber(*hp) << " is core of Vic " << (*vct) << " and hence HoI " << hoiCoreTag << "\n"; 
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
    
    string hoiOwnerTag = vicTagToHoiTagMap[bestOwnerTag];
    string hoiControllerTag = vicTagToHoiTagMap[bestControllerTag];
    (*hp)->resetLeaf("owner", addQuotes(hoiOwnerTag));
    (*hp)->resetLeaf("controller", addQuotes(hoiControllerTag));
    hoiCountryToHoiProvsMap[hoiTagToHoiCountryMap[hoiOwnerTag]].push_back(*hp); 
    Logger::logStream(Logger::Game) << "HoI province " << (*hp)->getKey()
				    << " owner, controller = Vic "
				    << bestOwnerTag << " -> HoI " << hoiOwnerTag << ", Vic "
				    << bestControllerTag << " -> HoI " << hoiControllerTag 
				    << " based on Vic population " << popOwnerMap[bestOwnerTag]
				    << ", " << popControllerMap[bestControllerTag] << ".\n"; 
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
    hoiCap->resetLeaf("capital", "yes"); 
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

  Logger::logStream(Logger::Debug) << "Resource totals:\n"; 
  for (map<string, double>::iterator r = hoiWorldTotals.begin(); r != hoiWorldTotals.end(); ++r) {
    string resName = (*r).first;
    Logger::logStream(Logger::Debug) << "  " << resName << " Vic: " << vicWorldTotals[resName] << " HoI: " << (*r).second << "\n"; 
  }
  
  map<string, map<string, double> > overflows; 
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
  if (!createProvinceMap()) return; 
  if (!createCountryMap()) return;
  initialiseVicSummaries(); 
  if (!convertProvinceOwners()) return;
  if (!moveCapitals()) return;
  if (!moveResources()) return; 
  cleanUp(); 
  
  Logger::logStream(Logger::Game) << "Done with conversion, writing to Output/converted.hoi3.\n";
 
  ofstream writer;
  writer.open(".\\Output\\converted.hoi3");
  Parser::topLevel = hoiGame;
  writer << (*hoiGame);
  writer.close();
  Logger::logStream(Logger::Game) << "Done writing.\n";
}
