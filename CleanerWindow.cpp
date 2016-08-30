#include "CleanerWindow.hh"
#include <QApplication>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QPainter> 
#include "Parser.hh"
#include <cstdio> 
#include <QtGui>
#include <QDesktopWidget>
#include <QRect>
#include <qpushbutton.h>
#include <iostream> 
#include <fstream> 
#include <string>
#include "Logger.hh" 
#include <set>
#include <algorithm>
#include "StructUtils.hh" 
#include "boost/tokenizer.hpp"
#include "PopMerger.hh" 
#include "StringManips.hh"

/*
  TODO 2: Effects on RGO production
   - Techs
  Infra from all POPs?
*/

using namespace std; 

char stringbuffer[10000]; 
int forty712s = 1; 
int maxMinisterId = 0;
int eventId = 3000001;

int main (int argc, char** argv) {
  QApplication industryApp(argc, argv);
  QDesktopWidget* desk = QApplication::desktop();
  QRect scr = desk->availableGeometry();
  CleanerWindow window;
  window.show();
  srand(42); 
  
  window.resize(3*scr.width()/5, scr.height()/2);
  window.move(scr.width()/5, scr.height()/4);
  window.setWindowTitle(QApplication::translate("toplevel", "Vicky to HoI converter"));
 
  QMenuBar* menuBar = window.menuBar();
  QMenu* fileMenu = menuBar->addMenu("File");
  QAction* newGame = fileMenu->addAction("Load file");
  QAction* quit    = fileMenu->addAction("Quit");

  QObject::connect(quit, SIGNAL(triggered()), &window, SLOT(close())); 
  QObject::connect(newGame, SIGNAL(triggered()), &window, SLOT(loadFile())); 

  QMenu* actionMenu = menuBar->addMenu("Actions");
  QAction* clean = actionMenu->addAction("Clean");
  QObject::connect(clean, SIGNAL(triggered()), &window, SLOT(cleanFile())); 
  QAction* stats = actionMenu->addAction("Stats");
  QObject::connect(stats, SIGNAL(triggered()), &window, SLOT(getStats()));
  QAction* convert = actionMenu->addAction("Convert");
  QObject::connect(convert, SIGNAL(triggered()), &window, SLOT(convert())); 
  QAction* merge = actionMenu->addAction("Merge POPs");
  QObject::connect(merge, SIGNAL(triggered()), &window, SLOT(mergePops()));
#ifdef DEBUG
  QAction* teams = actionMenu->addAction("Find teams");
  QObject::connect(teams, SIGNAL(triggered()), &window, SLOT(findTeams()));
#endif
  QAction* cmap = actionMenu->addAction("Colour map");
  QObject::connect(cmap, SIGNAL(triggered()), &window, SLOT(colourMap()));
  
  window.textWindow = new QPlainTextEdit(&window);
  window.textWindow->setFixedSize(3*scr.width()/5 - 10, scr.height()/2-40);
  window.textWindow->move(5, 30);
  window.textWindow->show(); 

  Logger::createStream(Logger::Debug);
  Logger::createStream(Logger::Trace);
  Logger::createStream(Logger::Game);
  Logger::createStream(Logger::Warning);
  Logger::createStream(Logger::Error);

  QObject::connect(&(Logger::logStream(Logger::Debug)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Trace)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Game)),    SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Warning)), SIGNAL(message(QString)), &window, SLOT(message(QString)));
  QObject::connect(&(Logger::logStream(Logger::Error)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));

  
  for (int i = DebugLeaders; i < NumDebugs; ++i) {
    Logger::createStream(i);
    QObject::connect(&(Logger::logStream(i)),   SIGNAL(message(QString)), &window, SLOT(message(QString)));
    Logger::logStream(i).setActive(false); 
  }
  
  
  window.show();
  if (argc > 1) window.loadFile(argv[1], argc > 2 ? atoi(argv[2]) : 1);  
  return industryApp.exec();  
}


CleanerWindow::CleanerWindow (QWidget* parent) 
  : QMainWindow(parent)
  , worker(0)
{}

CleanerWindow::~CleanerWindow () {}

void CleanerWindow::message (QString m) {
  textWindow->appendPlainText(m); 
}

void CleanerWindow::loadFile () {
  QString filename = QFileDialog::getOpenFileName(this, tr("Select file"), QString(""), QString("*.v2"));
  string fn = filename.toStdString();
  if (fn == "") return;
  loadFile(fn);   
}

void CleanerWindow::loadFile (string fname, int autoTask) {
  if (worker) delete worker;
  worker = new WorkerThread(fname, autoTask);
  worker->start();
}

void CleanerWindow::cleanFile () {
  Logger::logStream(Logger::Game) << "Starting clean.\n";
  worker->setTask(WorkerThread::CleanFile); 
  worker->start(); 
}

void CleanerWindow::getStats () {
  Logger::logStream(Logger::Game) << "Starting statistics.\n";
  worker->setTask(WorkerThread::Statistics); 
  worker->start(); 
}

void CleanerWindow::convert () {
  Logger::logStream(Logger::Game) << "Convert.\n";
  worker->setTask(WorkerThread::Convert); 
  worker->start(); 
}

void CleanerWindow::mergePops () {
  Logger::logStream(Logger::Game) << "Merge POPs.\n";
  worker->setTask(WorkerThread::MergePops); 
  worker->start(); 
}

void CleanerWindow::findTeams() {
  if (worker) delete worker;
  worker = new WorkerThread("blah"); 
  Logger::logStream(Logger::Game) << "Finding teams.\n";
  worker->setTask(WorkerThread::FindTeams); 
  worker->start(); 
}

void CleanerWindow::colourMap() {
  if (worker) delete worker;
  worker = new WorkerThread("blah"); 
  Logger::logStream(Logger::Game) << "Map colours.\n"; 
  worker->setTask(WorkerThread::ColourMap); 
  worker->start(); 
}

WorkerThread::WorkerThread (string fn, int atask)
  : targetVersion(".\\AoD\\")
  , sourceVersion(".\\V2\\")
  , fname(fn)
  , hoiGame(0)
  , vicGame(0)
  , task(LoadFile)
  , configObject(0)
  , customObject(0)
  , autoTask(atask)
  , popMerger(0)
{
  configure();
}  

WorkerThread::~WorkerThread () {
  if (hoiGame) delete hoiGame;
  if (vicGame) delete vicGame; 
  hoiGame = 0;
  vicGame = 0; 
}

void WorkerThread::run () {
  switch (task) {
  case LoadFile: loadFile(fname); break;
  case CleanFile: cleanFile(); break;
  case Statistics: getStatistics(); break;
  case Convert: convert(); break;
  case MergePops:
    if (popMerger) {
      fillVicVectors(); 
      setAcceptedStatus(); 
      popMerger->mergePops(vicCountries, vicProvinces);
    }
    else Logger::logStream(Logger::Error) << "Error: Pop merger not initialised - should have happened on load of savegame.\n";
    break;
  case FindTeams: findTeams(); break;
  case ColourMap: colourHoiMap(); break; 
  case NumTasks: 
  default: break; 
  }
}

string getField (string str, int field, char separator = ' ') {
  size_t start = 0;
  size_t stops = str.find(separator); 
  for (int i = 0; i < field; ++i) {
    start = stops+1;
    stops = str.find(separator, start); 
  }
  return str.substr(start, stops); 
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

pair<string, string> extractCulture (Object* pop) {
  if (pop->safeGetString("culture", "notset") != "notset") 
    return pair<string, string>(pop->safeGetString("culture"), pop->safeGetString("religion"));

  objvec leaves = pop->getLeaves();
  for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
    if (!(*l)->isLeaf()) continue;
    if ((*l)->getKey() == "id") continue; 
    if ((*l)->isNumeric()) continue;
    pop->resetLeaf("culture", (*l)->getKey());
    pop->resetLeaf("religion", (*l)->getLeaf());
    //Logger::logStream(Logger::Debug) << "Extracted culture " << (*l)->getKey() << "\n"; 
    return pair<string, string>((*l)->getKey(), (*l)->getLeaf());
  }
  return pair<string, string>("NEKULTURNY", "NORELIGION"); 
}

struct PopSizeInfo {
  PopSizeInfo () {
    binBoundaries.push_back(10);
    binBoundaries.push_back(100);
    binBoundaries.push_back(1000);
    binBoundaries.push_back(2000);
    binBoundaries.push_back(5000);
    binBoundaries.push_back(10000);
    binBoundaries.push_back(50000);
    binBoundaries.push_back(100000);

    sizes.resize(binBoundaries.size() + 1); 
  } 
  void fill (int size) {
    if (size > binBoundaries.back()) sizes[binBoundaries.size()]++;
    for (unsigned int i = 0; i < sizes.size(); ++i) {
      if (size > binBoundaries[i]) continue;
      sizes[i]++;
      break; 
    }
  }

  vector<double> binBoundaries;
  vector<int> sizes;
};

void WorkerThread::getStatistics () {
  if (!vicGame) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  map<string, PopSizeInfo*> sizeHists; 
  objvec leaves = vicGame->getLeaves();
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    objvec pops = (*leaf)->getLeaves();
    for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
      if ((*pop)->safeGetString("literacy", "NONE") == "NONE") continue;
      string ptype = (*pop)->getKey();
      if (0 == sizeHists[ptype]) sizeHists[ptype] = new PopSizeInfo(); 
      sizeHists[ptype]->fill((*pop)->safeGetInt("size")); 
    }
  }

  for (map<string, PopSizeInfo*>::iterator hist = sizeHists.begin(); hist != sizeHists.end(); ++hist) {
    Logger::logStream(Logger::Game) << (*hist).first << ":\n  0";
    for (unsigned int i = 0; i < (*hist).second->binBoundaries.size(); ++i) {
      Logger::logStream(Logger::Game) << "-" << (*hist).second->binBoundaries[i]
				      << " : "
				      << (*hist).second->sizes[i]
				      << "\n  "
				      << (*hist).second->binBoundaries[i];
    }
    Logger::logStream(Logger::Game) << "+ : "
				    << (*hist).second->sizes.back()
				    << "\n"; 
  }
 
  Logger::logStream(Logger::Game) << "Done with statistics.\n";
}

void WorkerThread::calculateGovTypes () {
  Object* matrix = loadTextFile(targetVersion + "matrix.txt");
  objvec rows;
  rows.push_back(matrix->safeGetObject("demo0"));
  rows.push_back(matrix->safeGetObject("demo1"));
  rows.push_back(matrix->safeGetObject("demo2"));
  rows.push_back(matrix->safeGetObject("demo3"));
  rows.push_back(matrix->safeGetObject("demo4"));
  rows.push_back(matrix->safeGetObject("demo5"));
  rows.push_back(matrix->safeGetObject("demo6"));
  rows.push_back(matrix->safeGetObject("demo7"));
  rows.push_back(matrix->safeGetObject("demo8"));
  rows.push_back(matrix->safeGetObject("demo9"));
  rows.push_back(matrix->safeGetObject("demo10"));
		 
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {    
    if ((*hoi)->safeGetString("tag") == "REB") continue;
    Object* policy = (*hoi)->safeGetObject("policy");
    Logger::logStream(Logger::Debug) << "Trying tag "
				     << (*hoi)->safeGetString("tag") << " "
				     << (int) (0 != policy) << "\n"; 
    if (!policy) continue;
    int demoSlider = policy->safeGetInt("democratic");
    int leftSlider = policy->safeGetInt("political_left");

    Object* theRow = rows[demoSlider];
    string value = theRow->getToken(10 - leftSlider);
    (*hoi)->resetLeaf("govType", value);
    Logger::logStream(Logger::Debug) << "Set " << (*hoi)->safeGetString("tag")
				     << " government to "
				     << value << " "
				     << demoSlider << " "
				     << leftSlider << ".\n"; 
  }
}

void WorkerThread::cleanFile () {
  if (!vicGame) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }

  bool provsFound[10000];
  for (int i = 0; i < 10000; ++i) {
    provsFound[i] = false; 
  }
  
  objvec leaves = vicGame->getLeaves();
  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    //Logger::logStream(Logger::Game) << (*leaf)->getKey() << "\n"; 
    if (3 != (*leaf)->getKey().length()) continue;
    if (0 != atoi((*leaf)->getKey().c_str())) continue;
    objvec states = (*leaf)->getValue("state");
    if (0 == states.size()) continue;
    for (objiter state = states.begin(); state != states.end(); ++state) {
      Object* provs = (*state)->safeGetObject("provinces");
      for (int i = 0; i < provs->numTokens(); ++i) {
	string provid = provs->getToken(i).c_str();
	int provnum = atoi(provid.c_str());
	if (provsFound[provnum]) {
	  Logger::logStream(Logger::Game) << "Problem: Province "
					  << provid
					  << " found in more than one state.\n"; 
	
	}
	provsFound[provnum] = true;

	Object* province = vicGame->safeGetObject(provid);
	if (!province) {
	  Logger::logStream(Logger::Game) << "Problem: Could not find object for province "
					  << provid
					  << ", claimed by "
					  << (*leaf)->getKey()
					  << ".\n";
	  continue;
	}
	string owner = remQuotes(province->safeGetString("owner", "\"NONE\""));
	if (((*leaf)->getKey() != owner) && (owner != "NONE")) {
	  Logger::logStream(Logger::Game) << "Problem: Province "
					  << provid
					  << " claimed by "
					  << (*leaf)->getKey()
					  << " but owner is "
					  << owner 
					  << ".\n"; 
	}
	Object* nation = vicGame->safeGetObject(owner);
	if ((owner != "NONE") && (!nation)) {
	  Logger::logStream(Logger::Game) << "Problem: Province "
					  << provid
					  << " owned by nonexistent nation "
					  << owner
					  << ".\n"; 
	}
      }
    }
  }

  for (objiter leaf = leaves.begin(); leaf != leaves.end(); ++leaf) {
    string owner = remQuotes((*leaf)->safeGetString("owner", "\"NONE\""));
    if (owner == "NONE") continue;
    string provid = (*leaf)->getKey();
    int provnum = atoi(provid.c_str());
    if (!provsFound[provnum]) {
      Logger::logStream(Logger::Game) << "Problem: Province "
				      << provid 
				      << " has owner but no state.\n"; 
    }
    Object* nation = vicGame->safeGetObject(owner);
    if (!nation) {
      Logger::logStream(Logger::Game) << "Problem: Province "
				      << provid
				      << " owned by nonexistent nation "
				      << owner
				      << ".\n"; 
    }
  }
  
  Logger::logStream(Logger::Game) << "Done cleaning.\n";
}

void WorkerThread::loadFile (string fname) {
  vicGame = loadTextFile(fname);
  Logger::logStream(Logger::Game) << "Done processing.\n";
  popMerger = new PopMerger(vicGame);   
  switch (autoTask) {
  case 1:
    task = Convert;
    convert(); 
    break;
    //case 2:
    //task = Statistics;
    //getStatistics();
    //break;
  case 4:
    task = MergePops;
    fillVicVectors(); 
    setAcceptedStatus();     
    popMerger->mergePops(vicCountries, vicProvinces);
    break;
  case 6:
    task = ColourMap;
    colourHoiMap(); 
    break;
  case -1: 
  default:
    break;
  }
}

QRgb makeRGB (string name) {
  if (name == "Gray") return qRgb(195, 195, 195);
  if (name == "DarkBrown") return qRgb(136, 0, 21);
  if (name == "LightRed") return qRgb(255, 174, 201);
  if (name == "LightYellow") return qRgb(163, 73, 164); // Special for Mongol Khanate
  if (name == "Blue") return qRgb(0, 162, 232);
  if (name == "DarkBlue") return qRgb(63, 72, 204);
  if (name == "LightBlue") return qRgb(153, 217, 234); 
  if (name == "DarkYellow") return qRgb(255, 127, 39);
  if (name == "Green") return qRgb(34, 177, 76);
  if (name == "Yellow") return qRgb(255, 242, 0);
  if (name == "LightBrown") return qRgb(112, 146, 190);
  //if (name == "") return qRgb();
  //if (name == "") return qRgb();
  //if (name == "") return qRgb();
  //if (name == "") return qRgb();  
  //if (name == "") return qRgb();  
  return qRgb(100, 100, 100); 
}

int floodFill (QImage& basicMap, QRgb colour1, QRgb colour2, int& xpixel, int& ypixel) { 
  int ret = 0; 
  QRgb initialColour = basicMap.pixel(xpixel, ypixel);
  vector<pair<int, int> > opened;
  vector<pair<int, int> > finded;  
  opened.push_back(pair<int, int>(xpixel, ypixel));
  static bool** closed = 0;
  if (!closed) {
    closed = new bool*[basicMap.width()];    
    for (int i = 0; i < basicMap.width(); ++i) {
      closed[i] = new bool[basicMap.height()];      
    }
  }

  for (int i = 0; i < basicMap.width(); ++i) {
    for (int j = 0; j < basicMap.height(); ++j) {
      closed[i][j] = false; 
    }
  }
  
  while (opened.size()) {
    int currX = opened.back().first;
    int currY = opened.back().second;
    finded.push_back(opened.back());     
    opened.pop_back();
    closed[currX][currY] = true;

    for (int plusX = -1; plusX <= 1; ++plusX) {
      int considerX = currX + plusX;
      if (considerX >= basicMap.width()) continue;
      if (considerX < 0) continue;
      
      for (int plusY = -1; plusY <= 1; ++plusY) {
	int considerY = currY + plusY;
	if (considerY >= basicMap.height()) continue;
	if (considerY < 0) continue;

	if (closed[considerX][considerY]) continue;
	QRgb pixColour = basicMap.pixel(considerX, considerY); 
	if (pixColour != initialColour) { // Found a boundary
	  closed[considerX][considerY] = true;
	  continue;
	}
	
	opened.push_back(pair<int, int>(considerX, considerY));
	closed[considerX][considerY] = true; 
      }
    }
  }

  int maxX = 0;
  int maxY = 0;
  for (vector<pair<int, int> >::iterator f = finded.begin(); f != finded.end(); ++f) {
    int currX = (*f).first;
    int currY = (*f).second;

    int stripe = currX - currY;
    if (stripe >= 0) basicMap.setPixel(currX, currY, stripe % 40 < 16 ? colour1 : colour2);
    else basicMap.setPixel(currX, currY, -stripe % 40 < 25 ? colour2 : colour1);
    //basicMap.setPixel(currX, currY, colour1); 
    ++ret;
    maxX = max(maxX, abs(xpixel - currX));
    maxY = max(maxY, abs(ypixel - currY)); 
  }

  xpixel = maxX;
  ypixel = maxY; 
  return ret;
}

int floodWithCheck (QImage& basicMap, QRgb colour1, QRgb colour2, int& xpixel, int& ypixel, Object* hpi) { 
  QRgb existingColour = basicMap.pixel(xpixel, ypixel);
  // Check for ocean grey - probably means a small island. Don't want to floodfill the whole ocean. 
  if ((abs(qBlue(existingColour)  - 127) < 2) && 
      (abs(qRed(existingColour)   - 127) < 2) &&
      (abs(qGreen(existingColour) - 127) < 2)) return 0;
    
  if (qBlue(existingColour) + qGreen(existingColour) + qRed(existingColour) < 10) {
    Logger::logStream(Logger::Game) << "Possible problem "
				    << xpixel << ", " << ypixel << " " 
				    << qAlpha(existingColour) << " " 
				    << qRed(existingColour) << " "
				    << qGreen(existingColour) << " "
				    << qBlue(existingColour) << " \n";
    return 0; 
  }
  return floodFill(basicMap, colour1, colour2, xpixel, ypixel);
}

void WorkerThread::colourHoiMap () {
  // NB, this cheats by loading a HoI save as the 'vicGame',
  // avoiding special code. 
  Object* hpinfo = loadTextFile(targetVersion + "province.txt");
  objvec hpis = hpinfo->getValue("province");
  double maxx = 0;
  double maxy = 0;
  double minx = 1e6;
  double miny = 1e6;

  vector<Object*> hpiArray;
  for (objiter hpi = hpis.begin(); hpi != hpis.end(); ++hpi) {
    double xcheck = (*hpi)->safeGetFloat("cityx");
    if (0 == xcheck) continue; // Avoid lakes and whatnot 
    
    maxx = max(maxx, (*hpi)->safeGetFloat("armyx"));
    maxy = max(maxy, (*hpi)->safeGetFloat("armyy"));
    minx = min(minx, (*hpi)->safeGetFloat("armyx"));
    miny = min(miny, (*hpi)->safeGetFloat("armyy"));

    int id = (*hpi)->safeGetInt("id", -1);
    if (-1 == id) continue;

    if (id >= (int) hpiArray.size()) hpiArray.resize(2*id);
    hpiArray[id] = (*hpi); 
  }

  Logger::logStream(Logger::Game) << "Maxima: " << maxx << ", " << maxy << "\n";
  Logger::logStream(Logger::Game) << "Minima: " << minx << ", " << miny << "\n";

  // Derived from map file being 7488x2880, and above printouts being close to 4 times these numbers.
  double realMaxX = 29952;
  double realMaxY = 11520; 

  int mapCols = configObject->safeGetInt("mapColours", 2); 
  objvec countries = vicGame->getValue("country");
  for (objiter country = countries.begin(); country != countries.end(); ++country) {
    string tag = (*country)->safeGetString("tag"); 
    Object* owned = (*country)->safeGetObject("ownedprovinces");
    if (owned) {
      for (int prov = 0; prov < owned->numTokens(); ++prov) {
	Object* hpi = hpiArray[owned->tokenAsInt(prov)];
	if (!hpi) continue;
	hpi->resetLeaf("owner", tag);
      }
    }
    owned = (*country)->safeGetObject("controlledprovinces");
    if (owned) {
      for (int prov = 0; prov < owned->numTokens(); ++prov) {
	Object* hpi = hpiArray[owned->tokenAsInt(prov)];
	if (!hpi) continue;
	hpi->resetLeaf("occupier", tag);
      }
    }    
  }
  
  map<string, QRgb> colourMap;
  Object* colourList = loadTextFile(targetVersion + "colours.txt");
  objvec colours = colourList->getLeaves();
  for (objiter col = colours.begin(); col != colours.end(); ++col) {
    colourMap[(*col)->getKey()] = makeRGB((*col)->getLeaf());
  }
  
  QImage basicMap;
  bool status = basicMap.load((targetVersion + "blankmap.png").c_str());
  Logger::logStream(Logger::Game) << "Image load: " << status << "\n";
 
  //QRgb sharpRed = qRgb(255, 0, 0); 
  for (objiter hpi = hpis.begin(); hpi != hpis.end(); ++hpi) {
    if ((*hpi)->safeGetString("terrain") == "\"Ocean\"") continue; 
   
    double xcoord = (*hpi)->safeGetFloat("armyx");
    xcoord /= realMaxX;
    xcoord *= basicMap.width();
    
    double ycoord = (*hpi)->safeGetFloat("armyy");
    ycoord /= realMaxY;
    ycoord *= basicMap.height();

    int xpixel = (int) floor(xcoord + 0.5);
    int ypixel = (int) floor(ycoord + 0.5);

    Logger::logStream(Logger::Game) << (*hpi)->safeGetString("id") << ": "
				    << (*hpi)->safeGetString("owner") << " "
				    << (*hpi)->safeGetString("occupier") << " ("
				    << xpixel << ", " << ypixel << ") "; 

    std::string second = "occupier";
    if (1 == mapCols) second = "owner"; 
    int numPixels = floodWithCheck(basicMap, colourMap[(*hpi)->safeGetString("owner")], colourMap[(*hpi)->safeGetString(second)], xpixel, ypixel, (*hpi));

    objvec extraX = (*hpi)->getValue("extraX");
    objvec extraY = (*hpi)->getValue("extraY");
    for (unsigned int e = 0; e < extraX.size(); ++e) {
      if (e >= extraY.size()) break;
      int currX = atoi(extraX[e]->getLeaf().c_str());
      int currY = atoi(extraY[e]->getLeaf().c_str()); // Don't transform these
      Logger::logStream(Logger::Game) << " (" << currX << " " << currY << ")"; 
      numPixels += floodWithCheck(basicMap, colourMap[(*hpi)->safeGetString("owner")], colourMap[(*hpi)->safeGetString(second)], currX, currY, (*hpi));
      xpixel += currX;
      ypixel += currY; 
    }
    
    Logger::logStream(Logger::Game) << numPixels << " " << xpixel << " " << ypixel << " " << (int) extraX.size() << "\n";
    //basicMap.setPixel((int) floor(xcoord + 0.5), (int) floor(ycoord + 0.5), sharpRed);
  }
  
  
  status = basicMap.save("copy.png");
  Logger::logStream(Logger::Game) << "Image save: " << status << "\n";  
}

void WorkerThread::findTeams () {
  hoiGame = processFile(targetVersion + "input.eug");
  hoiCountries = hoiGame->getValue("country");
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    objvec teams = (*hoi)->getValue("tech_team");
    for (objiter t = teams.begin(); t != teams.end(); ++t) {
      techTeams.push_back(*t);
      Object* idObject = (*t)->safeGetObject("id");
      if (idObject) (*t)->resetLeaf("id", idObject->safeGetString("id")); 
      Object* skills = (*t)->safeGetObject("research_types");
      if (!skills) continue;
      for (int i = 0; i < skills->numTokens(); ++i) (*t)->resetLeaf(skills->getToken(i), "yes"); 
    }
  }

  Object* techs = loadTextFile("./techGroups.txt");
  objvec groups = techs->getValue("techGroup");


  objvec hoiTechs; 
  Object* htechobj = loadTextFile(targetVersion + "techs.txt");
  objvec hoiTechAreas  = htechobj->getValue("technology");
  for (objiter hta = hoiTechAreas.begin(); hta != hoiTechAreas.end(); ++hta) {
    objvec apps = (*hta)->getValue("application");
    for (objiter a = apps.begin(); a != apps.end(); ++a) hoiTechs.push_back(*a); 
  }

  ObjectDescendingSorter sorter("suitability"); 
  for (objiter group = groups.begin(); group != groups.end(); ++group) {
    objvec current_techs;
    for (objiter t = hoiTechs.begin(); t != hoiTechs.end(); ++t) {
      string id = (*t)->safeGetString("id");
      bool inGroup = false;
      for (int i = 0; i < (*group)->numTokens(); ++i) {if (id == (*group)->getToken(i)) inGroup = true;}
      if (!inGroup) continue;
      current_techs.push_back(*t);
    }

    for (objiter team = techTeams.begin(); team != techTeams.end(); ++team) {
      if ((*team)->safeGetString("alreadyBid", "no") == "yes") {
	(*team)->resetLeaf("suitability", "0"); 
	continue;
      }
      double points = (*team)->safeGetFloat("skill");
      for (objiter tech = current_techs.begin(); tech != current_techs.end(); ++tech) {
	objvec comps = (*tech)->getValue("component");
	for (objiter comp = comps.begin(); comp != comps.end(); ++comp) {
	  if ((*team)->safeGetString((*comp)->safeGetString("type"), "no") != "yes") continue;
	  double mod = 0.5;
	  if ((*comp)->safeGetString("double_time", "no") == "yes") mod = 1; 
	  points += mod*(*comp)->safeGetFloat("difficulty");
	}
      }
      (*team)->resetLeaf("suitability", points); 
    }

    sort(techTeams.begin(), techTeams.end(), sorter);
    Logger::logStream(Logger::Game) << "    team = { ";
    for (int i = 0; i < 10; ++i) {
      Logger::logStream(Logger::Game) << techTeams[i]->safeGetString("id") << " ";
    }
    Logger::logStream(Logger::Game) << "}\n";
  }

  Logger::logStream(Logger::Game) << "Done.\n"; 
}

void WorkerThread::configure () {
  configObject = processFile("config.txt");
  targetVersion = configObject->safeGetString("outdir", ".\\AoD\\");
  sourceVersion = configObject->safeGetString("inpdir", ".\\V2\\");
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

  customObject = processFile(sourceVersion + configObject->safeGetString("kustomFile", "custom.txt")); 
}

Object* WorkerThread::loadTextFile (string fname) {
  Logger::logStream(Logger::Game) << "Parsing file " << fname << "\n";
  ifstream reader;
  //string dummy;
  reader.open(fname.c_str());
  //reader >> dummy;
  if ((reader.eof()) || (reader.fail())) {
    Logger::logStream(Logger::Error) << "Could not open file, returning null object.\n";
    return 0; 
  }
  
  Object* ret = processFile(fname);
  Logger::logStream(Logger::Game) << " ... done.\n";
  return ret; 
}

double MapInfo::distanceHoiToHoi (Object* hpi1, Object* hpi2) {
  double x1 = hpi1->safeGetFloat("armyx");
  double y1 = hpi1->safeGetFloat("armyy");
  double x2 = hpi2->safeGetFloat("armyx");
  double y2 = hpi2->safeGetFloat("armyy");

  double x2prime1 = x2 + hoiWidth;
  double x2prime2 = x2 - hoiWidth;
  
  double currDistSq = pow(y2 - y1, 2);
  currDistSq += min(min(pow(x1 - x2, 2), pow(x1 - x2prime1, 2)), pow(x1 - x2prime2, 2));
  return currDistSq; 
}

void extractVicPos (Object* vpi, double& xpos, double& ypos) {
  Object* coords = vpi->safeGetObject("unit");
  if (!coords) coords = vpi->safeGetObject("text_position");
  if (!coords) coords = vpi->safeGetObject("factory");
  if (!coords) coords = vpi->safeGetObject("building_construction");
  if (!coords) {
    Object* secondTry = vpi->safeGetObject("building_positions");
    if (secondTry) {
      coords = secondTry->safeGetObject("fort");
      if (!coords) coords = secondTry->safeGetObject("naval_base");
      if (!coords) coords = secondTry->safeGetObject("railroad");
    }
    if (!coords) {
      if (vpi->safeGetString("printed", "no") == "no") {
	Logger::logStream(Logger::Error) << "Error: Could not find coordinates of V2 province "
					 << vpi->getKey()
					 << ".\n";
	vpi->resetLeaf("printed", "yes"); 
      }
      xpos = 1e100;
      ypos = 1e100;
      return; 
    }
  }

  xpos = coords->safeGetFloat("x");
  ypos = coords->safeGetFloat("y");
}

double MapInfo::distanceHoiToVic (Object* hpi, Object* vpi) {
  double currX = hpi->safeGetFloat("armyx");
  double currY = hpi->safeGetFloat("armyy");

  double vicX1 = 0;
  double vicY = 0;
  extractVicPos(vpi, vicX1, vicY); 
  
  double scaleFactorX = (hoiWidth  / vicWidth);
  double scaleFactorY = (hoiHeight / vicHeight);

  double vicX2 = vicX1 + vicWidth;
  double vicX3 = vicX1 - vicWidth;
  vicX1 *= scaleFactorX;
  vicX2 *= scaleFactorX;
  vicX3 *= scaleFactorX;
  vicY   = vicHeight - vicY; // Vic map is upside down with respect to HoI map 
  vicY  *= scaleFactorY;    
  
  double currDistSq = pow(vicY - currY, 2);
  currDistSq += min(min(pow(currX - vicX1, 2), pow(currX - vicX2, 2)), pow(currX - vicX3, 2));
  return currDistSq; 
}


bool inhabited (Object* prov) {
  if (prov->safeGetInt("life_rating", -1) >= 0) return true;
  if (prov->getValue("labourers").size() > 0)   return true;
  if (prov->getValue("farmers").size() > 0)     return true;
  if (prov->getValue("artisans").size() > 0)    return true;
  return false; 
}

double simpleHoiDistance (Object* hpi1, Object* hpi2) {
  double x1 = hpi1->safeGetFloat("armyx");
  double y1 = hpi1->safeGetFloat("armyy");
  double x2 = hpi2->safeGetFloat("armyx");
  double y2 = hpi2->safeGetFloat("armyy");
  return (pow(x1 - x2, 2) + pow(y1 - y2, 2));
}

void WorkerThread::setAcceptedStatus () {
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    Object* owner = findVicCountryByVicTag((*vic)->safeGetString("owner"));
    if (!owner) continue;

    // Find cultures 
    string primary = remQuotes(owner->safeGetString("primary_culture"));
    vector<string> secondaries;
    Object* secs = owner->safeGetObject("culture");
    if (secs) {
      for (int i = 0; i < secs->numTokens(); ++i) {
	secondaries.push_back(remQuotes(secs->getToken(i))); 
      }
    }
    
    objvec leaves = (*vic)->getLeaves();
    for (objiter pop = leaves.begin(); pop != leaves.end(); ++pop) {
      double literacy = (*pop)->safeGetFloat("literacy", -1);
      if (0 > literacy) continue;
      double size = (*pop)->safeGetFloat("size", -1);
      if (0 > size) continue;
      bool isAccepted = false;
      if ((*pop)->safeGetString(primary, "BUH") != "BUH") isAccepted = true;
      else {
	for (vector<string>::iterator s = secondaries.begin(); s != secondaries.end(); ++s) {
	  if ((*pop)->safeGetString((*s), "BUH") == "BUH") continue;
	  isAccepted = true;
	  break;
	}
      }
      (*pop)->resetLeaf("acceptedCulture", isAccepted ? "yes" : "no");
    }
  }
}

map<Object*, Object*> hoiToVicKeyMap;
map<Object*, Object*> vicToHoiKeyMap;  
objvec vicInfos;
objvec hoiKeys;
objvec vicKeys; 

void WorkerThread::setKeys (Object* hpi, Object* vicKey) {
  hoiKeys.push_back(hpi);
  vicKeys.push_back(vicKey);
  double xperx = 0; 
  double ypery = 0;
  extractVicPos(vicKey, xperx, ypery); 
  ypery = mapInfo->vicHeight - ypery;
  xperx /= hpi->safeGetFloat("armyx");
  ypery /= hpi->safeGetFloat("armyy");
  
  hpi->resetLeaf("vicXperHoiX", xperx);
  hpi->resetLeaf("vicYperHoiY", ypery);
  
  vicKey->resetLeaf("vicXperHoiX", xperx);
  vicKey->resetLeaf("vicYperHoiY", ypery);
  
  hoiToVicKeyMap[hpi] = vicKey;
  vicToHoiKeyMap[vicKey] = hpi; 
}

void WorkerThread::createKeyList (Object* provNames) {
  Object* keyProvObject = loadTextFile(targetVersion + "keyProvinces.txt");
  objvec keyProvList = keyProvObject->getLeaves();
  Object* vpinfo = loadTextFile(sourceVersion + "vicpositions.txt");
  vicInfos = vpinfo->getLeaves();

  
  for (objiter keyProv = keyProvList.begin(); keyProv != keyProvList.end(); ++keyProv) {
    Object* hpi = findHoiProvInfoFromHoiId((*keyProv)->getLeaf());
    if (!hpi) continue;
    Object* vicKey = vpinfo->safeGetObject((*keyProv)->getKey());
    if (!vicKey) continue; 
    setKeys(hpi, vicKey); 
  }

  map<string, Object*> vicNameToPosMap; 
  for (objiter vpi = vicInfos.begin(); vpi != vicInfos.end(); ++vpi) {
    Object* vic = vicGame->safeGetObject((*vpi)->getKey());
    if (!vic) continue;
    string name = remQuotes(vic->safeGetString("name"));
    name = getField(name, 1, '_');
    vicNameToPosMap[name] = (*vpi); 
  }

  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    if ((*hpi)->safeGetString("terrain") == "\"Ocean\"") continue;
    if (hoiToVicKeyMap[*hpi]) continue; 
    Object* hoiName = provNames->safeGetObject((*hpi)->safeGetString("id"));
    if (!hoiName) continue; 
    string name = remQuotes(hoiName->getLeaf()); 
    Object* vpi = vicNameToPosMap[name];
    if (!vpi) continue;
    Logger::logStream(DebugProvinces) << "Matching province names: " << name << "\n";
    setKeys((*hpi), vpi); 
  }

}


void WorkerThread::recalculateProvinceMapNew () {  
  Object* keyProvObject = loadTextFile(targetVersion + "keyProvinces.txt");
  objvec keyProvList = keyProvObject->getLeaves();
  assert(0 < keyProvList.size());
  Object* vpinfo = loadTextFile(sourceVersion + "vicpositions.txt");
  objvec vicInfos = vpinfo->getLeaves();  
  objvec hoiKeys;
  objvec vicKeys; 


  for (objiter keyProv = keyProvList.begin(); keyProv != keyProvList.end(); ++keyProv) {
    Object* hpi = findHoiProvInfoFromHoiId((*keyProv)->getLeaf());
    if (!hpi) continue;
    Object* vicKey = vpinfo->safeGetObject((*keyProv)->getKey());
    if (!vicKey) continue; 
    
    hoiKeys.push_back(hpi);
    vicKeys.push_back(vicKey);
    Logger::logStream(DebugProvinces) << "Found keys "
				      << vicKey->getKey() << " "
				      << hpi->safeGetString("id")
				      << ".\n"; 
    double xperx = 0; 
    double ypery = 0;
    extractVicPos(vicKey, xperx, ypery); 
    ypery = mapInfo->vicHeight - ypery;
    xperx /= hpi->safeGetFloat("armyx");
    ypery /= hpi->safeGetFloat("armyy");

    hpi->resetLeaf("vicXperHoiX", xperx);
    hpi->resetLeaf("vicYperHoiY", ypery);

    vicKey->resetLeaf("vicXperHoiX", xperx);
    vicKey->resetLeaf("vicYperHoiY", ypery);        
  }

  assert(hoiKeys.size()); 

  map<Object*, objvec> vicToHois;
  map<Object*, objvec> hoiToVics; 
  
  Object* provMapObject = new Object("provMapObject");   
  map<Object*, bool> vicProvsAssigned;  
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    if ((*hpi)->safeGetString("terrain") == "\"Ocean\"") continue;         

    Logger::logStream(DebugProvinces) << "Processing HoI province " << (*hpi)->safeGetString("id") << ".\n";
    
    // Find closest key province, use corresponding transform
    double dist = 1e200;
    Object* closestKey = hoiKeys[0]; 
    for (objiter keyProv = hoiKeys.begin(); keyProv != hoiKeys.end(); ++keyProv) {
      double currDist = simpleHoiDistance((*hpi), (*keyProv));
      if (currDist > dist) continue;
      dist = currDist;
      closestKey = (*keyProv); 
    }
    

    double vicX = (*hpi)->safeGetFloat("armyx")*closestKey->safeGetFloat("vicXperHoiX");
    double vicY = (*hpi)->safeGetFloat("armyy")*closestKey->safeGetFloat("vicYperHoiY");
    vicY = mapInfo->vicHeight - vicY; 
    
    // Find closest Vic province
    dist = 1e200;
    Object* closestVic = vicInfos[0];
    for (objiter vic = vicInfos.begin(); vic != vicInfos.end(); ++vic) {
      Object* currVP = vicGame->safeGetObject((*vic)->getKey());
      if (!currVP) continue; 
      if (currVP->safeGetString("owner", "NONE") == "NONE") continue; 
      if (!inhabited(currVP)) continue;
      
      double currX = 0;
      double currY = 0;
      extractVicPos((*vic), currX, currY);
      double currDist = pow(currX - vicX, 2) + pow(currY - vicY, 2);
      if (currDist > dist) continue;
      dist = currDist;
      closestVic = (*vic); 
    }

    Logger::logStream(DebugProvinces) << "Key is "
				      << closestKey->safeGetString("id") 
				      << " with ("
				      << closestKey->safeGetFloat("vicXperHoiX") << ", "
				      << closestKey->safeGetFloat("vicYperHoiY") << ") ("
				      << vicX << ", "
				      << vicY << ") ("
				      << (*hpi)->safeGetFloat("armyx") << ", "
				      << (*hpi)->safeGetFloat("armyy") << ") "
				      << closestVic->getKey() << ".\n ";

    
    vicProvsAssigned[closestVic] = true;
    hoiToVics[*hpi].push_back(closestVic);
    vicToHois[closestVic].push_back(*hpi); 
    Logger::logStream(DebugProvinces) << "Assigned HoI " << (*hpi)->safeGetString("id") << " to Vic " << closestVic->getKey() << ".\n";        
  }

  // Now the converse: Assign any Vicky provinces that weren't done in the first pass
  for (objiter vic = vicInfos.begin(); vic != vicInfos.end(); ++vic) {
    if (vicProvsAssigned[*vic]) continue; 
    Object* currVP = vicGame->safeGetObject((*vic)->getKey());
    if (!currVP) continue; 
    if (!inhabited(currVP)) continue;

    Logger::logStream(DebugProvinces) << "Processing Vic province " << (*vic)->getKey() << ".\n";
    
    double vicX = 0;
    double vicY = 0;
    extractVicPos((*vic), vicX, vicY);
    Object* closestVicKey = vicKeys[0];
    double dist = 1e200;
    for (objiter key = vicKeys.begin(); key != vicKeys.end(); ++key) {
      double currX = 0;
      double currY = 0;
      extractVicPos((*key), currX, currY);
      double currDist = pow(currX - vicX, 2) + pow(currY - vicY, 2);
      Logger::logStream(DebugProvinces) << "Considering key "
					<< (*key)->getKey() << " ("
					<< currX << ", " << currY << ") "
					<< currDist << "("
					<< vicX << ", " << vicY << ")\n"; 
      if (currDist > dist) continue;
      closestVicKey = (*key);
      dist = currDist; 
    }

    double hoiX = vicX / closestVicKey->safeGetFloat("vicXperHoiX");
    double hoiY = (mapInfo->vicHeight - vicY) / closestVicKey->safeGetFloat("vicYperHoiY");

    Logger::logStream(DebugProvinces) << " Found key "
				      << closestVicKey->getKey() << " ("
				      << closestVicKey->safeGetString("vicXperHoiX") << ", "
				      << closestVicKey->safeGetString("vicYperHoiY") << ") ("
				      << hoiX << ", " << hoiY << ") ("
				      << vicX << ", " << vicY << ")\n"; 
    
    // Now find closest HoI province.
    dist = 1e200;
    Object* bestHoiProvince = hoiProvInfos[0]; 
    for (objiter hoi = hoiProvInfos.begin(); hoi != hoiProvInfos.end(); ++hoi) {
      if ((*hoi)->safeGetString("terrain") == "\"Ocean\"") continue;         

      double currX = (*hoi)->safeGetFloat("armyx");
      double currY = (*hoi)->safeGetFloat("armyy");      
      double currDist = pow(currX - hoiX, 2) + pow(currY - hoiY, 2);
      if (currDist > dist) continue;
      dist = currDist;
      bestHoiProvince = (*hoi); 
    }
    hoiToVics[bestHoiProvince].push_back(*vic);
    vicToHois[*vic].push_back(bestHoiProvince); 
    Logger::logStream(DebugProvinces) << "Assigned HoI " << bestHoiProvince->safeGetString("id") << " to Vic " << (*vic)->getKey() << ".\n";    
  }

  // Check for Vic provinces with single HoI provinces that are also
  // assigned to other Vic provinces which have more than one HoI province.
  for (map<Object*, objvec>::iterator vth = vicToHois.begin(); vth != vicToHois.end(); ++vth) {
    if (1 < (*vth).second.size()) continue; 
    // This Vic has only one HoI.

    Object* hoi = ((*vth).second)[0]; 
    // Does the HoI have any other Vics?
    if (1 == hoiToVics[hoi].size()) continue; 

    Object* thisVic = (*vth).first; 
    // If those Vics have other HoIs, they don't need this one.
    for (objiter otherVic = hoiToVics[hoi].begin(); otherVic != hoiToVics[hoi].end(); ++otherVic) {
      if (thisVic == (*otherVic)) continue;
      if (1 == vicToHois[*otherVic].size()) continue;

      for (unsigned int i = 0; i < vicToHois[*otherVic].size(); ++i) {
	if (vicToHois[*otherVic][i] != hoi) continue;
	vicToHois[*otherVic][i] = vicToHois[*otherVic].back();
	vicToHois[*otherVic].pop_back();
	break; 
      }
    }
  }


  for (map<Object*, objvec>::iterator vth = vicToHois.begin(); vth != vicToHois.end(); ++vth) {
    Object* vic = (*vth).first; 
    
    Object* currLink = provMapObject->safeGetObject(vic->getKey());
    if (!currLink) {
      currLink = new Object(vic->getKey());
      provMapObject->setValue(currLink);
    }

    for (objiter hoi = (*vth).second.begin(); hoi != (*vth).second.end(); ++hoi) {
      currLink->setLeaf("hoi", (*hoi)->safeGetString("id"));
    }
  }

  // Add comments
  map<string, int> regionToIndexMap; 
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    string region = (*hpi)->safeGetString("name");
    if (regionToIndexMap[region]) continue;
    regionToIndexMap[region] = 1 + regionToIndexMap.size();
  }
  
  objvec links = provMapObject->getLeaves();
  for (objiter link = links.begin(); link != links.end(); ++link) {
    Object* hoi = findHoiProvinceFromHoiId((*link)->safeGetString("hoi"));
    Object* hpi = findHoiProvInfoFromHoiProvince(hoi);
    (*link)->setLeaf("hoiRegion", hpi->safeGetString("name"));
    (*link)->setLeaf("region", regionToIndexMap[hpi->safeGetString("name")]);
  }
  ObjectAscendingSorter regionSorter("region"); 
  sort(links.begin(), links.end(), regionSorter);

  for (objiter link = links.begin(); link != links.end(); ++link) {
    (*link)->unsetValue("region"); 
  }

  Object* provNames = loadTextFile(targetVersion + "provNames.txt");
  for (objiter link = links.begin(); link != links.end(); ++link) {
    string vicTag = (*link)->getKey();
    Object* vicProv = vicGame->safeGetObject(vicTag);
    if (vicProv) (*link)->setComment(vicProv->safeGetString("name"));

    objvec hois = (*link)->getValue("hoi");
    for (objiter hoi = hois.begin(); hoi != hois.end(); ++hoi) {
      (*hoi)->setComment(remQuotes(provNames->safeGetString((*hoi)->getLeaf())));
    }
  }


  
  ofstream writer;
  writer.open((targetVersion + "province_mapping.txt").c_str());
  Parser::topLevel = provMapObject;
  writer << (*provMapObject);
  writer.close();
  delete provMapObject; 
}

void WorkerThread::recalculateProvinceMapTriangulate () {  
  Object* provNames = loadTextFile(targetVersion + "provNames.txt");
  createKeyList(provNames);   

  map<Object*, objvec> vicToHois;
  map<Object*, objvec> hoiToVics; 
  
  Object* provMapObject = new Object("provMapObject");   
  map<Object*, bool> vicProvsAssigned;
  ObjectAscendingSorter sorter("distance");
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    if ((*hpi)->safeGetString("terrain") == "\"Ocean\"") continue;         

    Logger::logStream(DebugProvinces) << "Processing HoI province " << (*hpi)->safeGetString("id") << ".\n";
    
    // Find closest key provinces, triangulate
    for (objiter keyProv = hoiKeys.begin(); keyProv != hoiKeys.end(); ++keyProv) {
      double currDist = simpleHoiDistance((*hpi), (*keyProv));
      (*keyProv)->resetLeaf("distance", currDist);
    }

    sort(hoiKeys.begin(), hoiKeys.end(), sorter); 

    bool debug = ((*hpi)->safeGetString("id") == "31"); 

    if (debug) Logger::logStream(DebugProvinces) << "HoI province "
						 << (*hpi)->safeGetString("id") << "("
						 << (*hpi)->safeGetFloat("armyx") << ", "
						 << (*hpi)->safeGetFloat("armyy") << "):\n"; 

    
    double vicX = 0;
    double vicY = 0;
    double totalWeight = 0; 
    for (unsigned int i = 0; i < min((unsigned int) 3, hoiKeys.size()); ++i) {
      double vicOffsetX = (*hpi)->safeGetFloat("armyx") - hoiKeys[i]->safeGetFloat("armyx");
      vicOffsetX *= hoiKeys[i]->safeGetFloat("vicXperHoiX");
      double vicOffsetY = (*hpi)->safeGetFloat("armyy") - hoiKeys[i]->safeGetFloat("armyy");
      vicOffsetY *= hoiKeys[i]->safeGetFloat("vicYperHoiY");      

      Object* currVicKey = hoiToVicKeyMap[hoiKeys[i]]; 
      double currVicX = 0;
      double currVicY = 0; 
      extractVicPos(currVicKey, currVicX, currVicY);
     
      currVicX += vicOffsetX;
      currVicY += vicOffsetY;

      double weight = 0.01 + hoiKeys[i]->safeGetFloat("distance");
      weight = 1.0 / weight;
      vicX += weight * currVicX;
      vicY += weight * currVicY;
      totalWeight += weight;

      if (debug) Logger::logStream(DebugProvinces) << "  Key number " << (int) i << " Hoi: "
						   << hoiKeys[i]->safeGetString("id") << " ("
						   << hoiKeys[i]->safeGetFloat("armyx") << ", "
						   << hoiKeys[i]->safeGetFloat("armyy") << ") "
						   << hoiKeys[i]->safeGetString("distance") << " " << weight << " " << totalWeight << " Vic: "
						   << currVicKey->getKey() << " "
						   << vicGame->safeGetObject(currVicKey->getKey())->safeGetString("name") << " ("
						   << currVicX - vicOffsetX << ", " << currVicY - vicOffsetY << ") ("
						   << vicOffsetX << ", " << vicOffsetY << ") ("
						   << vicX/totalWeight << " " << vicY/totalWeight << ") ("
						   << currVicX << ", " << currVicY
						   << ")\n"; 

    }
    vicX /= totalWeight;
    vicY /= totalWeight;    

    if (debug) Logger::logStream(DebugProvinces) << "  Final Vic: " << vicX << ", " << vicY << "\n"; 
    
    //vicY = mapInfo->vicHeight - vicY; 
    
    // Find closest Vic province
    double dist = 1e200;
    Object* closestVic = vicInfos[0];
    for (objiter vic = vicInfos.begin(); vic != vicInfos.end(); ++vic) {
      Object* currVP = vicGame->safeGetObject((*vic)->getKey());
      if (!currVP) continue; 
      if (currVP->safeGetString("owner", "NONE") == "NONE") continue; 
      if (!inhabited(currVP)) continue;
      
      double currX = 0;
      double currY = 0;
      extractVicPos((*vic), currX, currY);
      double currDist = pow(currX - vicX, 2) + pow(currY - vicY, 2);
      if (currDist > dist) continue;
      dist = currDist;
      closestVic = (*vic); 
    }
   
    vicProvsAssigned[closestVic] = true;
    hoiToVics[*hpi].push_back(closestVic);
    vicToHois[closestVic].push_back(*hpi); 
    Logger::logStream(DebugProvinces) << "Assigned HoI " << (*hpi)->safeGetString("id") << " to Vic " << closestVic->getKey() << ".\n";        
  }

  // Now the converse: Assign any Vicky provinces that weren't done in the first pass
  for (objiter vic = vicInfos.begin(); vic != vicInfos.end(); ++vic) {
    if (vicProvsAssigned[*vic]) continue; 
    Object* currVP = vicGame->safeGetObject((*vic)->getKey());
    if (!currVP) continue; 
    if (!inhabited(currVP)) continue;

    Logger::logStream(DebugProvinces) << "Processing Vic province " << (*vic)->getKey() << ".\n";
    
    double vicX = 0;
    double vicY = 0;
    extractVicPos((*vic), vicX, vicY);
    for (objiter key = vicKeys.begin(); key != vicKeys.end(); ++key) {
      double currX = 0;
      double currY = 0;
      extractVicPos((*key), currX, currY);
      double currDist = pow(currX - vicX, 2) + pow(currY - vicY, 2);
      (*key)->resetLeaf("distance", currDist); 
    }

    sort(vicKeys.begin(), vicKeys.end(), sorter);
    vicY = mapInfo->vicHeight - vicY; 
    
    double totalWeight = 0;
    double hoiX = 0;
    double hoiY = 0;
    for (unsigned int i = 0; i < min((unsigned int) 3, vicKeys.size()); ++i) {
      double currVicX = 0;
      double currVicY = 0; 
      extractVicPos(vicKeys[i], currVicX, currVicY); 
      currVicY = mapInfo->vicHeight - currVicY; 
      
      double hoiOffsetX = vicX - currVicX; 
      hoiOffsetX /= vicKeys[i]->safeGetFloat("vicXperHoiX");
      double hoiOffsetY = vicY - currVicY; 
      hoiOffsetY *= vicKeys[i]->safeGetFloat("vicYperHoiY");      

      Object* currHoiKey = vicToHoiKeyMap[vicKeys[i]]; 
      double currHoiX = currHoiKey->safeGetFloat("armyx");
      double currHoiY = currHoiKey->safeGetFloat("armyy");
      
      currHoiX += hoiOffsetX;
      currHoiY += hoiOffsetY;

      double weight = 0.01 + vicKeys[i]->safeGetFloat("distance");
      weight = 1.0 / weight;
      hoiX += weight * currHoiX;
      hoiY += weight * currHoiY;
      totalWeight += weight; 
    }
    
    hoiX /= totalWeight;
    hoiY /= totalWeight;    
   
    // Now find closest HoI province.
    double dist = 1e200;
    Object* bestHoiProvince = hoiProvInfos[0]; 
    for (objiter hoi = hoiProvInfos.begin(); hoi != hoiProvInfos.end(); ++hoi) {
      if ((*hoi)->safeGetString("terrain") == "\"Ocean\"") continue;         

      double currX = (*hoi)->safeGetFloat("armyx");
      double currY = (*hoi)->safeGetFloat("armyy");      
      double currDist = pow(currX - hoiX, 2) + pow(currY - hoiY, 2);
      if (currDist > dist) continue;
      dist = currDist;
      bestHoiProvince = (*hoi); 
    }
    hoiToVics[bestHoiProvince].push_back(*vic);
    vicToHois[*vic].push_back(bestHoiProvince); 
    Logger::logStream(DebugProvinces) << "Assigned HoI " << bestHoiProvince->safeGetString("id") << " to Vic " << (*vic)->getKey() << ".\n";    
  }

  // Check for Vic provinces with single HoI provinces that are also
  // assigned to other Vic provinces which have more than one HoI province.
  for (map<Object*, objvec>::iterator vth = vicToHois.begin(); vth != vicToHois.end(); ++vth) {
    if (1 < (*vth).second.size()) continue; 
    // This Vic has only one HoI.

    Object* hoi = ((*vth).second)[0]; 
    // Does the HoI have any other Vics?
    if (1 == hoiToVics[hoi].size()) continue; 

    Object* thisVic = (*vth).first; 
    // If those Vics have other HoIs, they don't need this one.
    for (objiter otherVic = hoiToVics[hoi].begin(); otherVic != hoiToVics[hoi].end(); ++otherVic) {
      if (thisVic == (*otherVic)) continue;
      if (1 == vicToHois[*otherVic].size()) continue;

      for (unsigned int i = 0; i < vicToHois[*otherVic].size(); ++i) {
	if (vicToHois[*otherVic][i] != hoi) continue;
	vicToHois[*otherVic][i] = vicToHois[*otherVic].back();
	vicToHois[*otherVic].pop_back();
	break; 
      }
    }
  }


  for (map<Object*, objvec>::iterator vth = vicToHois.begin(); vth != vicToHois.end(); ++vth) {
    Object* vic = (*vth).first; 
    
    Object* currLink = provMapObject->safeGetObject(vic->getKey());
    if (!currLink) {
      currLink = new Object(vic->getKey());
      provMapObject->setValue(currLink);
    }

    for (objiter hoi = (*vth).second.begin(); hoi != (*vth).second.end(); ++hoi) {
      currLink->setLeaf("hoi", (*hoi)->safeGetString("id"));
    }
  }

  // Add comments
  map<string, int> regionToIndexMap; 
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    string region = (*hpi)->safeGetString("name");
    if (regionToIndexMap[region]) continue;
    regionToIndexMap[region] = 1 + regionToIndexMap.size();
  }
  
  objvec links = provMapObject->getLeaves();
  for (objiter link = links.begin(); link != links.end(); ++link) {
    Object* hoi = findHoiProvinceFromHoiId((*link)->safeGetString("hoi"));
    Object* hpi = findHoiProvInfoFromHoiProvince(hoi);
    (*link)->setLeaf("hoiRegion", hpi->safeGetString("name"));
    (*link)->setLeaf("region", regionToIndexMap[hpi->safeGetString("name")]);
  }
  ObjectAscendingSorter regionSorter("region"); 
  sort(links.begin(), links.end(), regionSorter);

  for (objiter link = links.begin(); link != links.end(); ++link) {
    (*link)->unsetValue("region"); 
  }

  for (objiter link = links.begin(); link != links.end(); ++link) {
    string vicTag = (*link)->getKey();
    Object* vicProv = vicGame->safeGetObject(vicTag);
    if (vicProv) (*link)->setComment(vicProv->safeGetString("name"));

    objvec hois = (*link)->getValue("hoi");
    for (objiter hoi = hois.begin(); hoi != hois.end(); ++hoi) {
      (*hoi)->setComment(remQuotes(provNames->safeGetString((*hoi)->getLeaf())));
    }
  }


  
  ofstream writer;
  writer.open((targetVersion + "province_mapping.txt").c_str());
  Parser::topLevel = provMapObject;
  writer << (*provMapObject);
  writer.close();
  delete provMapObject; 
}


void WorkerThread::recalculateProvinceMap () {  
  double leastx = 10000;
  double mostx  = 0;
  double leasty = 10000;
  double mosty = 0;
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    leastx = min(leastx, (*hpi)->safeGetFloat("armyx", leastx));
    mostx  = max(mostx,  (*hpi)->safeGetFloat("armyx", mostx));
    leasty = min(leasty, (*hpi)->safeGetFloat("armyy", leasty));
    mosty  = max(mosty,  (*hpi)->safeGetFloat("armyy", mosty));
  }
  Logger::logStream(Logger::Game) << leastx << " " << mostx << " " << leasty << " " << mosty << "\n";
  mapInfo->hoiWidth  = mostx;
  mapInfo->hoiHeight = mosty;
  
  Object* vpinfo = loadTextFile(sourceVersion + "vicpositions.txt"); 
  objvec vicProvInfos = vpinfo->getLeaves();

  map<Object*, Object*> tempHoiToLinkMap;
  map<Object*, Object*> tempVicInfoToProvMap;
  map<Object*, Object*> tempHoiProvToInfoMap; 
  int ownedVicProvs = 0;
  int ownedHoiProvs = 0; 

    // Create province-to-information maps, first Vicky
  for (objiter vpi = vicProvInfos.begin(); vpi != vicProvInfos.end(); ++vpi) {
    Object* currVP = vicGame->safeGetObject((*vpi)->getKey());
    if (!currVP) continue;

    if (!inhabited(currVP)) continue;
    ownedVicProvs++;
    tempVicInfoToProvMap[*vpi] = currVP;
  }

  // Then HoI. 
  for (objiter hprov = hoiProvinces.begin(); hprov != hoiProvinces.end(); ++hprov) {
    string hoiTag = (*hprov)->safeGetString("id"); 
    objiter hpi;
    for (hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
      if ((*hpi)->safeGetString("id") != hoiTag) continue;
      break; 
    }
    if (hpi == hoiProvInfos.end()) { 
      Logger::logStream(Logger::Error) << "Error: Could not find information for HoI province "
				       << (*hprov)->safeGetString("id")
				       << ".\n";
      continue; 
    }
    tempHoiProvToInfoMap[*hprov] = (*hpi);
  }
  
  Object* provMapObject = new Object("provMapObject"); 
  for (objiter hprov = hoiProvinces.begin(); hprov != hoiProvinces.end(); ++hprov) {
    Object* hpi = tempHoiProvToInfoMap[*hprov];
    if (!hpi) continue;
    if (hpi->safeGetString("terrain") == "\"Ocean\"") continue;     
    Logger::logStream(Logger::Game) << "Processing province " << (*hprov)->safeGetString("id") << "\n"; 
    
    ownedHoiProvs++; 
    double leastDistSq = 1e100;
    Object* vicProv = 0;
    for (objiter vpi = vicProvInfos.begin(); vpi != vicProvInfos.end(); ++vpi) {
      Object* currVP = tempVicInfoToProvMap[*vpi];
      if (!currVP) continue; 
      if (currVP->safeGetString("owner", "NONE") == "NONE") continue; 
      if (!inhabited(currVP)) continue;
      
      double currDistSq = mapInfo->distanceHoiToVic(hpi, (*vpi)); 
      if (currDistSq > leastDistSq) continue;
      leastDistSq = currDistSq;
      vicProv = currVP; 
    }

    if (!vicProv) {
      Logger::logStream(Logger::Error) << "Error: Could not find V2 province for "
				       << (*hprov)->safeGetString("id") 
				       << ".\n";
      continue; 
    }

    Object* currLink = provMapObject->safeGetObject(vicProv->getKey());
    if (!currLink) {
      currLink = new Object(vicProv->getKey());
      provMapObject->setValue(currLink);
    }
    currLink->setLeaf("hoi", (*hprov)->safeGetString("id"));
    tempHoiToLinkMap[(*hprov)] = currLink; 
  }


  Logger::logStream(Logger::Game) << "Provinces : "
				  << ownedHoiProvs << " " 
				  << ownedVicProvs
				  << ".\n"; 

  
  for (objiter vpi = vicProvInfos.begin(); vpi != vicProvInfos.end(); ++vpi) {
    if (provMapObject->safeGetObject((*vpi)->getKey())) continue;
    Object* currVP = tempVicInfoToProvMap[*vpi];
    if (!currVP) continue; 
    if (!inhabited(currVP)) continue;
    
    Logger::logStream(Logger::Game) << "Fixing vic province "
				    << (*vpi)->getKey()
				    << ".\n"; 
    
    Object* link = new Object((*vpi)->getKey());
    provMapObject->setValue(link);

    Object* bestHoiProv = 0;
    double leastDistSq = 1e100; 
    for (objiter hprov = hoiProvinces.begin(); hprov != hoiProvinces.end(); ++hprov) {
      Object* hpi = tempHoiProvToInfoMap[*hprov];
      if (!hpi) continue;
      if (hpi->safeGetString("terrain") == "\"Ocean\"") continue;
      
      double currDistSq = mapInfo->distanceHoiToVic(hpi, (*vpi)); 
      if (currDistSq > leastDistSq) continue;

      leastDistSq = currDistSq;
      bestHoiProv = (*hprov); 	
    }
    //assert(bestHoiProv);
    if (!bestHoiProv) bestHoiProv->resetLeaf("balH", "dsahjk"); 
    string hoiTag = bestHoiProv->safeGetString("id"); 
    link->resetLeaf("hoi", hoiTag); 

    Object* oldLink = tempHoiToLinkMap[bestHoiProv];
    if (!oldLink) {
      
      Logger::logStream(Logger::Error) << "Error: Could not find link for "
				       << bestHoiProv->safeGetString("id")
				       << "\n";
      oldLink->resetLeaf("Beh", "DSA"); 
    }
    if (1 == oldLink->getValue("hoi").size()) continue; 
    objvec hois = oldLink->getValue("hoi");
    oldLink->clear();
    for (objiter hoi = hois.begin(); hoi != hois.end(); ++hoi) {
      if (hoiTag == (*hoi)->getLeaf()) continue;
      oldLink->setLeaf("hoi", (*hoi)->getLeaf()); 
    }

    tempHoiToLinkMap[bestHoiProv] = link; 
  }

  // Add comments
  map<string, int> regionToIndexMap; 
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    string region = (*hpi)->safeGetString("name");
    if (regionToIndexMap[region]) continue;
    regionToIndexMap[region] = 1 + regionToIndexMap.size();
  }
  
  objvec links = provMapObject->getLeaves();
  for (objiter link = links.begin(); link != links.end(); ++link) {
    Object* hoi = findHoiProvinceFromHoiId((*link)->safeGetString("hoi"));
    Object* hpi = findHoiProvInfoFromHoiProvince(hoi);
    (*link)->setLeaf("hoiRegion", hpi->safeGetString("name"));
    (*link)->setLeaf("region", regionToIndexMap[hpi->safeGetString("name")]);
  }
  ObjectAscendingSorter regionSorter("region"); 
  sort(links.begin(), links.end(), regionSorter);

  for (objiter link = links.begin(); link != links.end(); ++link) {
    (*link)->unsetValue("region"); 
  }

  Object* provNames = loadTextFile(targetVersion + "provNames.txt");
  for (objiter link = links.begin(); link != links.end(); ++link) {
    string vicTag = (*link)->getKey();
    Object* vicProv = vicGame->safeGetObject(vicTag);
    if (vicProv) (*link)->setComment(vicProv->safeGetString("name"));

    objvec hois = (*link)->getValue("hoi");
    for (objiter hoi = hois.begin(); hoi != hois.end(); ++hoi) {
      (*hoi)->setComment(remQuotes(provNames->safeGetString((*hoi)->getLeaf())));
    }
  }

  ofstream writer;
  writer.open((targetVersion + "province_mapping.txt").c_str());
  Parser::topLevel = provMapObject;
  writer << (*provMapObject);
  writer.close();
  delete provMapObject; 
}

void WorkerThread::fillVicVectors () {
  objvec vleaves = vicGame->getLeaves();
  Logger::logStream(Logger::Debug) << "Leaves: " << (int) vleaves.size() << "\n"; 
  
  for (objiter vl = vleaves.begin(); vl != vleaves.end(); ++vl) {
    if ((*vl)->safeGetString("capital", "-1") == "-1") continue;
    if ((*vl)->safeGetString("wage_reform", "blah") == "blah") continue;
    vicCountries.push_back(*vl);
    vicTagToVicCountryMap[(*vl)->getKey()] = (*vl);
  }

  for (objiter vl = vleaves.begin(); vl != vleaves.end(); ++vl) {
    if ((*vl)->safeGetString("owner", "NONE") == "NONE") continue;
    Logger::logStream(Logger::Debug) << (*vl)->getKey() << " " << (*vl)->safeGetString("owner") << "\n";
    tagToSizeMap[remQuotes((*vl)->safeGetString("owner"))] += 1;
    vicProvinces.push_back(*vl);     
  }

  configObject->resetLeaf("current_date_days", days(remQuotes(vicGame->safeGetString("date", "\"1936.1.1\"")))); 
}

Object* WorkerThread::findHoiProvinceFromHoiId (string id) {
  static map<string, Object*> idToProvinceMap;
  if (idToProvinceMap[id]) return idToProvinceMap[id];

  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    if (id != (*hoi)->safeGetString("id")) continue;
    idToProvinceMap[id] = (*hoi);
    return (*hoi);
  }

  Logger::logStream(Logger::Error) << "Error: Could not find-by-id HoI province "
				   << id
				   << ", returning null.\n";
  
  return 0; 
}

Object* WorkerThread::findHoiProvInfoFromHoiId (string id) {
  static map<string, Object*> idToProvInfoMap;
  if (idToProvInfoMap[id]) return idToProvInfoMap[id];

  for (objiter hoi = hoiProvInfos.begin(); hoi != hoiProvInfos.end(); ++hoi) {
    if (id != (*hoi)->safeGetString("id")) continue;
    idToProvInfoMap[id] = (*hoi);
    return (*hoi);
  }

  Logger::logStream(Logger::Error) << "Error: Could not find-by-id HoI provinfo "
				   << id
				   << ", returning null.\n";
  
  return 0; 
}

Object* WorkerThread::findHoiProvInfoFromHoiProvince (Object* hoi) {
  if (!hoi) return 0;
 
  static map<Object*, Object*> hoiProvinceToProvInfoMap; 
  if (hoiProvinceToProvInfoMap[hoi]) return hoiProvinceToProvInfoMap[hoi];
  
  Object* hoiInfo = 0; 
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    if ((*hpi)->safeGetString("id") != hoi->safeGetString("id")) continue;
    hoiInfo = (*hpi);
    break;
  }
 
  hoiProvinceToProvInfoMap[hoi] = hoiInfo; 
  return hoiInfo; 
}

Object* WorkerThread::findHoiProvinceFromHoiProvInfo (Object* hpi) {
  if (!hpi) return 0;
 
  static map<Object*, Object*> hoiProvInfoToProvinceMap; 
  if (hoiProvInfoToProvinceMap[hpi]) return hoiProvInfoToProvinceMap[hpi];
  
  Object* prov = 0; 
  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    if ((*hoi)->safeGetString("id") != hpi->safeGetString("id")) continue;
    prov = (*hoi);
    break;
  }
 
  hoiProvInfoToProvinceMap[hpi] = prov; 
  return prov; 
}

Object* WorkerThread::findHoiCapitalFromVicCountry (Object* vic) {
  string vicCapId = vic->safeGetString("capital", "NONE");
  Object* vicCap = vicGame->safeGetObject(vicCapId);
  if (!vicCap) {
    Logger::logStream(Logger::Error) << "Error : Could not find capital of Vic tag "
				     << vic->getKey()
				     << ", alleged to be "
				     << vicCapId
				     << ".\n";
    return 0; 
  }
  
  Object* hoiCap = 0;
  if (0 < vicProvinceToHoiProvincesMap[vicCap].size()) hoiCap = vicProvinceToHoiProvincesMap[vicCap][0];
  if (!hoiCap) {
    Logger::logStream(Logger::Error) << "Error : Could not find HoI capital of Vic tag "
				     << vic->getKey()
				     << ", Vicky province number "
				     << vicCapId
				     << ".\n";
  }

  return hoiCap; 
}

void WorkerThread::createProvinceMappings () {
  mapInfo = new MapInfo(); 
  mapInfo->vicWidth  = configObject->safeGetFloat("vicmapwidth", 5616);
  mapInfo->vicHeight = configObject->safeGetFloat("vicmapheight", 2160);
  
  hoiProvinces = hoiGame->getValue("province");
  Object* hpinfo = loadTextFile(targetVersion + "province.txt");
  hoiProvInfos = hpinfo->getValue("province");
  if (configObject->safeGetString("recalculateProvinces", "no") == "old") recalculateProvinceMap();
  else if (configObject->safeGetString("recalculateProvinces", "no") == "new") recalculateProvinceMapNew();
  else if (configObject->safeGetString("recalculateProvinces", "no") == "triangulate") recalculateProvinceMapTriangulate();  

  map<string, Object*> tagToProvMap; 
  Object* handLinks = loadTextFile(targetVersion + "handLinks.txt"); 
  objvec hlinks = handLinks->getLeaves();
  for (objiter link = hlinks.begin(); link != hlinks.end(); ++link) {
    Object* vic = vicGame->safeGetObject((*link)->getKey());
    if (!vic) continue;
    if (0 < vicProvinceToHoiProvincesMap[vic].size()) {
      Logger::logStream(Logger::Warning) << "Warning: Hand links attempt to redefine links for vic province "
					 << vic->getKey()
					 << ".\n"; 
      continue;
    }
    objvec hois = (*link)->getValue("hoi");
    for (objiter htag = hois.begin(); htag != hois.end(); ++htag) {
      for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
	if ((*hoi)->safeGetString("id") != (*htag)->getLeaf()) continue;
	tagToProvMap[(*hoi)->safeGetString("id")] = (*hoi); 	
	hoiProvinceToVicProvincesMap[*hoi].push_back(vic); 
	vicProvinceToHoiProvincesMap[vic].push_back(*hoi);
	Logger::logStream(DebugProvinces) << "Assigned "
					  << vic->getKey()
					  << " to "
					  << (*hoi)->safeGetString("id") << " "
					  << (int) vicProvinceToHoiProvincesMap[vic].size() 
					  << ".\n"; 
	break; 
      }
    }
  }
  
  Object* provMapFile = loadTextFile(targetVersion + "province_mapping.txt");
  mapInfo->hoiWidth  = provMapFile->safeGetFloat("hoiWidth");
  mapInfo->hoiHeight = provMapFile->safeGetFloat("hoiHeight");

  objvec links = provMapFile->getLeaves();
  for (objiter link = links.begin(); link != links.end(); ++link) {
    Logger::logStream(DebugProvinces) << "Second try for province " << (*link)->getKey() << "\n";
    Object* vic = vicGame->safeGetObject((*link)->getKey());
    if (!vic) continue;
    Logger::logStream(DebugProvinces) << "  Exists\n";
    if (0 < vicProvinceToHoiProvincesMap[vic].size()) continue; // Already assigned by humans.
    Logger::logStream(DebugProvinces) << "  Not assigned\n";
    objvec hois = (*link)->getValue("hoi");
    for (objiter htag = hois.begin(); htag != hois.end(); ++htag) {
      if (tagToProvMap[(*htag)->getLeaf()]) continue; // Already assigned by humans. 
      
      for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
	if ((*hoi)->safeGetString("id") != (*htag)->getLeaf()) continue;
	hoiProvinceToVicProvincesMap[*hoi].push_back(vic); 
	vicProvinceToHoiProvincesMap[vic].push_back(*hoi);
	Logger::logStream(DebugProvinces) << "Assigned 2 "
					  << vic->getKey()
					  << " to "
					  << (*hoi)->safeGetString("id") << " "
					  << (int) vicProvinceToHoiProvincesMap[vic].size() 
					  << ".\n"; 	
	break; 
      }
    }
  }

  // Look for HoI provinces not assigned by hand links and missed by autolinks
  // because their Vic province *was* assigned. Also look for Vic province missed
  // because their single HoI province was human-assigned. 
  for (objiter link = links.begin(); link != links.end(); ++link) {
    Object* vic = vicGame->safeGetObject((*link)->getKey());
    if (!vic) continue;

    bool vicNeeded = (0 == vicProvinceToHoiProvincesMap[vic].size()); 
    
    objvec hois = (*link)->getValue("hoi");
    for (objiter htag = hois.begin(); htag != hois.end(); ++htag) {
      Object* hoiProv = 0; 
      for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
	if ((*hoi)->safeGetString("id") != (*htag)->getLeaf()) continue;
	hoiProv = (*hoi);
	break;
      }

      if (!hoiProv) continue;
      if ((!vicNeeded) && (0 < hoiProvinceToVicProvincesMap[hoiProv].size())) continue;

      hoiProvinceToVicProvincesMap[hoiProv].push_back(vic); 
      vicProvinceToHoiProvincesMap[vic].push_back(hoiProv);
      Logger::logStream(DebugProvinces) << "Assigned 3 "
					<< vic->getKey()
					<< " to "
					<< hoiProv->safeGetString("id") << " "
					<< (int) vicProvinceToHoiProvincesMap[vic].size() 
					<< ".\n"; 	
    }
  }

  Logger::logStream(Logger::Game) << "Done with province mappings.\n";
}


void WorkerThread::assignCountries (Object* hoi, Object* vic) {
  hoi->setLeaf("victag", vic->getKey());
  vic->setLeaf("hoitag", hoi->safeGetString("tag"));
  Logger::logStream(Logger::Debug) << "Assigned V2 "
				   << vic->getKey()
				   << " to HoI "
				   << hoi->safeGetString("tag")
				   << ".\n"; 
}

		      
void WorkerThread::createCountryMappings () {
  Object* countryMap = loadTextFile(sourceVersion + "country_mappings.txt"); 
  
  hoiCountries = hoiGame->getValue("country");
  for (objiter hc = hoiCountries.begin(); hc != hoiCountries.end(); ++hc) {
    string htag = (*hc)->safeGetString("tag");
    if (htag == "REB") continue; 
    hoiTagToHoiCountryMap[htag] = (*hc); 
  }
  
  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {      
    string htag = countryMap->safeGetString((*vc)->getKey(), "NONE");
    if (htag != "NONE") {
      Object* hcountry = findHoiCountryByHoiTag(htag);
      if (hcountry) assignCountries(hcountry, (*vc));
      else {
	Object* copyThis = 0; 
	for (objiter cand = hoiCountries.begin(); cand != hoiCountries.end(); ++cand) {
	  if ((*cand)->safeGetString("tag") == "REB") continue;
	  copyThis = (*cand); 
	  break;
	}
	if (!copyThis) continue;
	hcountry = new Object(copyThis);
	Logger::logStream(Logger::Game) << "Created new country for tag " << htag << "\n";
	hcountry->resetLeaf("tag", htag);
	hcountry->unsetValue("leader");
	hcountry->unsetValue("minister");
	hcountry->unsetValue("tech_team");
	hcountry->unsetValue("landunit");
	hcountry->unsetValue("navalunit");
	hcountry->unsetValue("airunit");
	hoiTagToHoiCountryMap[htag] = hcountry;
	assignCountries(hcountry, (*vc));
	hoiCountries.push_back(hcountry);
	hoiGame->setValue(hcountry, copyThis); 
      }
    }
  }

  objiter despair = hoiCountries.begin(); 
  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {
    // Each HoI country may have multiple Vicky countries, but each Vicky
    // country has exactly one HoI country. 
    
    // Did anyone assign a country already? 
    if (findHoiCountryByVicCountry(*vc)) continue;

    // Check that this country is actually interesting - ignore ones with zero owned provinces.
    if (0 == tagToSizeMap[(*vc)->getKey()]) continue; 
    
    // Find the first available.
    for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
      if ((*hoi)->safeGetString("tag") == "REB") continue; 
      if (findVicCountryByHoiCountry(*hoi)) continue;
      assignCountries((*hoi), (*vc));
      break; 
    }
    if (findHoiCountryByVicCountry(*vc)) continue;

    // Is the Vicky country a vassal? If so, merge with overlord; or vice-versa. 
    Object* dip = vicGame->safeGetObject("diplomacy");
    if (dip) {
      objvec vassals = dip->getValue("vassal");
      for (objiter vas = vassals.begin(); vas != vassals.end(); ++vas) {
	Object* overlord = findVicCountryByVicTag(remQuotes((*vas)->safeGetString("first")));
	Object* vassal   = findVicCountryByVicTag(remQuotes((*vas)->safeGetString("second")));

	Object* otherHoiCountry = 0; 
	if (overlord == (*vc)) otherHoiCountry = findHoiCountryByVicCountry(vassal);
	else if (vassal == (*vc)) otherHoiCountry = findHoiCountryByVicCountry(overlord); 
	if (!otherHoiCountry) continue;
	
	
	assignCountries(otherHoiCountry, (*vc));
	break; 
      }
    }
    if (findHoiCountryByVicCountry(*vc)) continue;

    // Ok, just merge with closest capital.
    Object* hoiCap = findHoiCapitalFromVicCountry(*vc);
    if (!hoiCap) {
      Logger::logStream(Logger::Error) << "Error: Could not find capital of "
				       << (*vc)->getKey() << ", cannot assign reasonable conversion.\n";
      continue;
    }
    Object* hoiCapInfo = findHoiProvInfoFromHoiProvince(hoiCap); 
    if (!hoiCapInfo) {
      Logger::logStream(Logger::Error) << "Error: Could not find information for HoI province "
				       << hoiCap->safeGetString("id")
				       << ", capital of Vic tag "
				       << (*vc)->getKey()
				       << ", cannot assign reasonable conversion.\n";
    }
    else {
      double bestDist = 1e100;
      Object* closestHoi = 0;
      for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
	if ((*vic) == (*vc)) continue;
	if (!findHoiCountryByVicCountry(*vic)) continue;
	Object* secondHoiCap = findHoiCapitalFromVicCountry(*vic);
	if (!secondHoiCap) continue;
	Object* hoiCapInfo2 = findHoiProvInfoFromHoiProvince(secondHoiCap); 
	if (!hoiCapInfo2) continue;
	double distance = mapInfo->distanceHoiToHoi(hoiCapInfo, hoiCapInfo2);
	if (distance > bestDist) continue;
	bestDist = distance;
	closestHoi = findHoiCountryByVicCountry(*vic);
      }
      if (closestHoi) assignCountries(closestHoi, (*vc));
      else Logger::logStream(Logger::Debug) << "Could not find closest-capital candidate for "
					    << (*vc)->getKey()
					    << ", HoI capital is "
					    << (hoiCap ? hoiCap->safeGetString("id") : string("null"))
					    << ".\n"; 
    }

    if (findHoiCountryByVicCountry(*vc)) continue;
    Logger::logStream(Logger::Error) << "Error: Unable to find sensible HoI country for Vic tag "
				     << (*vc)->getKey()
				     << ", will use "
				     << (*despair)->safeGetString("tag")
				     << ".\n";

    assignCountries((*despair), (*vc));
    ++despair;
    if ((*despair)->safeGetString("tag") == "REB") ++despair; 
    if (despair == hoiCountries.end()) despair = hoiCountries.begin();
  }

  if (configObject->safeGetString("writeCountryMappings", "no") == "yes") {
    Object* countryMapObject = new Object("countries");
    for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
      objvec vics = (*hoi)->getValue("victag");
      for (objiter vc = vics.begin(); vc != vics.end(); ++vc) {
	Object* link = new Object((*vc)->getLeaf());
	link->resetLeaf("hoi", (*vc)->getLeaf());
	countryMapObject->setValue(link);
      }
    }
    
    ofstream writer;
    writer.open((sourceVersion + "new_country_mapping.txt").c_str());
    Parser::topLevel = countryMapObject;
    writer << (*countryMapObject);
    writer.close();
    delete countryMapObject; 
  }
  
  
  Logger::logStream(Logger::Game) << "Done with country mappings.\n";
}

void WorkerThread::createUtilityMaps () {
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    objvec states = (*vic)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      double aristos = 0;
      double total = 1; 
      Object* provlist = (*state)->safeGetObject("provinces");
      if (!provlist) continue; 
      for (int i = 0; i < provlist->numTokens(); ++i) {
	string provid = provlist->getToken(i);
	Object* province = vicGame->safeGetObject(provid);
	if (!province) continue;
	vicProvinceToVicStateMap[province] = (*state);
	if (find(vicStates.begin(), vicStates.end(), (*state)) == vicStates.end()) vicStates.push_back(*state);
	objvec pops = province->getLeaves();
	for (objiter p = pops.begin(); p != pops.end(); ++p) {
	  double size = (*p)->safeGetFloat("size");
	  if (1 > size) continue;
	  if ((*p)->getKey() == "aristocrats") aristos += size;
	  total += size; 
	}
      }
      aristos /= total;
      for (int i = 0; i < provlist->numTokens(); ++i) {
	string provid = provlist->getToken(i);
	Object* province = vicGame->safeGetObject(provid);
	if (!province) continue;
	province->resetLeaf("aristocratmod", aristos); 
      }
    }
  }
}

void WorkerThread::cleanUp () {
  for (objiter hc = hoiCountries.begin(); hc != hoiCountries.end(); ++hc) {
    Object* vicCountry = findVicCountryByVicTag((*hc)->safeGetString("victag"));
    if ((vicCountry) && (vicCountry->safeGetString("human", "no") == "yes")) (*hc)->unsetValue("ai"); 
    
    (*hc)->unsetValue("victag");
    (*hc)->unsetValue("moddingPoints");    
    (*hc)->unsetValue("customTechTeams");
    (*hc)->unsetValue("customMinisters");
    (*hc)->unsetValue("customOfficers");
    (*hc)->unsetValue("numOwned");
    (*hc)->unsetValue("mountains"); 
    (*hc)->unsetValue("vicmilsize");
    (*hc)->unsetValue("ruling_party");
    (*hc)->unsetValue("SpyInfo");
    (*hc)->unsetValue("division_development");
    (*hc)->unsetValue("application");
    (*hc)->unsetValue("govType");
    (*hc)->unsetValue("mobManpower");
    (*hc)->unsetValue("totalPop"); 
    (*hc)->unsetValue("totalIndustry"); 
    (*hc)->unsetValue("peacetime_ic_mod");
    (*hc)->unsetValue("officerNames"); 
    
    Object* teams = (*hc)->safeGetObject("team_slots");
    if (teams) for (int i = 0; i < teams->numTokens(); ++i) teams->resetToken(i, "0");
  }

  Object* newEvents = configObject->safeGetObject("eventList");
  if (newEvents) {
    hoiGame->unsetValue("event");
    Object* inter = hoiGame->safeGetObject("interface");
    objvec evts = newEvents->getValue("event");
    for (objiter evt = evts.begin(); evt != evts.end(); ++evt) {
      hoiGame->setValue((*evt), inter);
    } 
  }
  
  Object* battles = hoiGame->safeGetObject("battlehistory");
  if (battles) battles->unsetValue("recordedleaderevent"); 
  
  Object* goodsWeights = configObject->safeGetObject("goodsWeights");
  objvec goodsTypes;
  if (goodsWeights) goodsTypes = goodsWeights->getLeaves(); 
  for (objiter hp = hoiProvinces.begin(); hp != hoiProvinces.end(); ++hp) {
    if (goodsWeights) {
      double industry = (*hp)->safeGetInt("industry", -1);
      if (0 > industry) industry = 0; 
      Object* ic = (*hp)->safeGetObject("ic");
      if (!ic) {
	ic = new Object("ic");
	ic->setLeaf("type", "ic");
	ic->setLeaf("location", (*hp)->safeGetString("id"));
	(*hp)->setValue(ic);
      }
      ic->resetLeaf("size", industry);
      ic->resetLeaf("current_size", industry); 

      for (vector<string>::iterator good = hoiProducts.begin(); good != hoiProducts.end(); ++good) {
	(*hp)->unsetValue(*good); 
      }
    }

    (*hp)->unsetValue("owner");
    (*hp)->unsetValue("controller");
    (*hp)->unsetValue("vic_radio_factory");
    (*hp)->unsetValue("vic_synthetic_oil_factory");
    (*hp)->unsetValue("province_effectivity");
    (*hp)->unsetValue("antiairweight");
    (*hp)->unsetValue("landFortPriority");
    (*hp)->resetLeaf("province_revoltrisk", 0); 
  }

  Object* header = hoiGame->safeGetObject("header");
  Object* fortyID = header->safeGetObject("id");
  if (!fortyID) {
    fortyID = new Object("id");
    header->setValue(fortyID);
    fortyID->setLeaf("type", "4712");
  }
  fortyID->resetLeaf("id", forty712s); 
}

double WorkerThread::days (string datestring) {
  boost::char_separator<char> sep(".");
  boost::tokenizer<boost::char_separator<char> > tokens(datestring, sep);
  boost::tokenizer<boost::char_separator<char> >::iterator i = tokens.begin();
  int year = atoi((*i).c_str()); ++i;
  if (i == tokens.end()) {
    /*
    Logger::logStream(Logger::Warning) << "Attempt to use bad string \""
				       << datestring
				       << "\" as date, returning -1.\n";
    */ 
    return -1;
  }
  int month = atoi((*i).c_str()); ++i;
  if (i == tokens.end()) {
    /*
    Logger::logStream(Logger::Warning) << "Attempt to use bad string \""
				       << datestring
				       << "\" as date, returning -1.\n";
    */
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

Object* WorkerThread::findHoiCountryByHoiTag (string tag) {
  return hoiTagToHoiCountryMap[tag]; 
}

Object* WorkerThread::findVicCountryByVicTag (string tag) {
  Object* ret = vicTagToVicCountryMap[tag]; 
  if (ret) return ret;
  ret = vicTagToVicCountryMap[remQuotes(tag)];
  return ret; 
}

Object* makeIdObject (int id, int type) {
  Object* ret = new Object("id");
  ret->setLeaf("id", id);
  ret->setLeaf("type", type);
  return ret; 
}


Object* WorkerThread::findVicCountryByHoiCountry (Object* hoi) {
  string tag = hoi->safeGetString("victag", "NONE");
  if (tag == "NONE") return 0;
  return findVicCountryByVicTag(tag); 

}

Object* WorkerThread::findHoiCountryByVicCountry (Object* vic) {
  string tag = vic->safeGetString("hoitag", "NONE");
  if (tag == "NONE") return 0;
  return findHoiCountryByHoiTag(tag); 
}

string WorkerThread::findVicTagFromHoiTag (string hoitag, bool quotes) {
  Object* hoiCountry = findHoiCountryByHoiTag(hoitag);
  Object* vicCountry = findVicCountryByHoiCountry(hoiCountry);
  if (!vicCountry) return "NONE"; 
  string ret = vicCountry->getKey();
  if (!quotes) return ret;
  return addQuotes(ret); 
}

double provinceWeight (Object* vicprov) {
  return 1; 
}

void WorkerThread::addProvinceToHoiNation (Object* hoiProv, Object* hoiCountry) {
  Object* owned = hoiCountry->safeGetObject("ownedprovinces"); 
  owned->addToList(hoiProv->safeGetString("id"));
  owned = hoiCountry->safeGetObject("controlledprovinces");
  owned->addToList(hoiProv->safeGetString("id"));


  Logger::logStream(DebugProvinces) << "Adding province " << hoiProv->safeGetString("id") 
				    << " to nation "
				    << hoiCountry->safeGetString("tag") << " "
				    << owned->numTokens() 
				    << ".\n";

  static map<string, Object*> regionMap; 
  
  hoiProv->resetLeaf("owner", hoiCountry->safeGetString("tag"));
  Object* hpi = findHoiProvInfoFromHoiProvince(hoiProv);
  if (hpi) {
    hpi->resetLeaf("owner", hoiCountry->safeGetString("tag"));
    if (hpi->safeGetString("terrain") == "\"Mountain\"") hoiCountry->resetLeaf("mountains", hoiCountry->safeGetInt("mountains") + 2);
    else if (hpi->safeGetString("terrain") == "\"Mountain\"") hoiCountry->resetLeaf("mountains", hoiCountry->safeGetInt("Hills") + 1);
    string regionName = remQuotes(hpi->safeGetString("region"));
    Object* region = regionMap[regionName];
    if (!region) {
      region = new Object("hoi_region");
      regionMap[regionName] = region;
    }
    region->setValue(hpi); 
    hpi->setValue(region); // This would cause big trouble if I printed the province info objects. 
  }
  hoiCountry->resetLeaf("numOwned", 1 + hoiCountry->safeGetInt("numOwned"));
  
}

void WorkerThread::addProvinceToVicNation (Object* hoiProv, Object* vicNation) {
  Object* hoiCountry = findHoiCountryByVicCountry(vicNation);
  if (!hoiCountry) {
    Logger::logStream(Logger::Error) << "Error: Could not find Vic owner for HoI province "
				     << hoiProv->safeGetString("id") << " "
				     << vicNation->getKey()
				     << "\n";
    return; 
  }
  
  addProvinceToHoiNation(hoiProv, hoiCountry); 
}

void WorkerThread::moveControls () { 
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* controlled = (*hoi)->safeGetObject("controlledprovinces");
    if (!controlled) {
      controlled = new Object("controlledprovinces");
      controlled->setObjList(true); 
      (*hoi)->setValue(controlled); 
    }
    controlled->clear();
  }

  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    if (!(*hoi)->safeGetObject("ic")) continue; 
    
    objvec vics = hoiProvinceToVicProvincesMap[*hoi];
    map<Object*, double> weights;
    pair<Object*, double> best(0, 0); 
    for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
      Object* vicCountry = findVicCountryByVicTag((*vic)->safeGetString("controller", "NONE")); 
      if (!vicCountry) continue;

      weights[vicCountry] += provinceWeight(*vic);
      if (weights[vicCountry] < best.second) continue;
      best.second = weights[vicCountry];
      best.first = vicCountry; 
    }

    if (!best.first) continue;    
    Object* hoiCountry = findHoiCountryByVicCountry(best.first);
    if (!hoiCountry) continue; 
    
    (*hoi)->resetLeaf("controller", hoiCountry->safeGetString("tag"));
    hoiCountry->safeGetObject("controlledprovinces")->addToList((*hoi)->safeGetString("id")); 
  }

  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    if (!(*hoi)->safeGetObject("ic")) continue; 
    if ((*hoi)->safeGetString("controller", "BLAH") != "BLAH") continue;
    string owner = (*hoi)->safeGetString("owner", "BLAH");
    if (owner == "BLAH") continue;
    Object* hoiCountry = findHoiCountryByHoiTag(owner);
    hoiCountry->safeGetObject("controlledprovinces")->addToList((*hoi)->safeGetString("id"));
    (*hoi)->resetLeaf("controller", owner); 
  }
}

void WorkerThread::addCultureCores () {
  double minimumForCore = configObject->safeGetFloat("minimumForCore", 0.3); 
  map<string, int> cultures;
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    double total = 0;
    cultures.clear();
    objvec pops = (*vic)->getLeaves();
    for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
      if ((*pop)->safeGetString("literacy", "NONE") == "NONE") continue;
      if (0 == (*pop)->safeGetObject("issues")) continue;

      pair<string, string> cultrel = extractCulture(*pop);
      cultures[cultrel.first] += (*pop)->safeGetInt("size");
      total += (*pop)->safeGetInt("size");
    }

    for (map<string, int>::iterator i = cultures.begin(); i != cultures.end(); ++i) {
      if (minimumForCore > (*i).second / total) continue;
      (*vic)->resetLeaf((*i).first, "isBigCulture"); 
    }
  }
  
  for (objiter vicCountry = vicCountries.begin(); vicCountry != vicCountries.end(); ++vicCountry) {
    string tag = (*vicCountry)->getKey();
    if (tag == "REB") continue; 
    string primary = remQuotes((*vicCountry)->safeGetString("primary_culture"));
    vector<string> secondaries;
    secondaries.push_back(primary);
    Object* secs = (*vicCountry)->safeGetObject("culture");
    if (secs) {
      for (int i = 0; i < secs->numTokens(); ++i) {
	secondaries.push_back(remQuotes(secs->getToken(i))); 
      }
    }

    for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
      if (remQuotes((*vic)->safeGetString("owner", "\"NONE\"")) != tag) continue; 
      objvec cores = (*vic)->getValue("core");
      bool hasCore = false;
      for (objiter core = cores.begin(); core != cores.end(); ++core) {
	if (tag != remQuotes((*core)->getLeaf())) continue;
	hasCore = true;
	break;
      }
      if (hasCore) continue;

      for (vector<string>::iterator c = secondaries.begin(); c != secondaries.end(); ++c) {
	if ((*vic)->safeGetString((*c), "nuffink") != "isBigCulture") continue;
	(*vic)->setLeaf("core", addQuotes(tag));
	Logger::logStream(DebugCores) << "Made vic province "
				      << (*vic)->getKey() << " "
				      << (*vic)->safeGetString("name")
				      << " core of "
				      << tag << " due to culture "
				      << (*c)
				      << "\n"; 
	break; 
      }
    }
  }
}

void WorkerThread::moveCores () {
  addCultureCores(); 
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* national = (*hoi)->safeGetObject("nationalprovinces");
    if (!national) continue;
    national->clear(); 
  }
  
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    objvec cores = (*vic)->getValue("core");
    for (objiter c = cores.begin(); c != cores.end(); ++c) {
      string tag = remQuotes((*c)->getLeaf());
      Object* vicCore = findVicCountryByVicTag(tag);
      if (!vicCore) continue;
      Object* hoiCore = findHoiCountryByVicCountry(vicCore);
      if (!hoiCore) continue;
      Object* national = hoiCore->safeGetObject("nationalprovinces");
      if (!national) {
	national = new Object("nationalprovinces");
	hoiCore->setValue(national);
      }

      for (objiter hoi = vicProvinceToHoiProvincesMap[*vic].begin(); hoi != vicProvinceToHoiProvincesMap[*vic].end(); ++hoi) {
	if (!(*hoi)) {
	  Logger::logStream(Logger::Debug) << "Null HoI province for vic province "
					   << (*vic)->getKey()
					   << ", size is " << (int) vicProvinceToHoiProvincesMap[*vic].size()
					   << "\n";
	  continue; 
	}
	string hoiID = (*hoi)->safeGetString("id");
	bool gotCore = false;
	for (int i = 0; i < national->numTokens(); ++i) {
	  if (hoiID != national->getToken(i)) continue;
	  gotCore = true;
	  break;
	}
	if (gotCore) continue;
	national->addToList(hoiID); 
      }
    }
  }
}

void WorkerThread::moveProvinces () { 
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* owned = (*hoi)->safeGetObject("ownedprovinces");
    if (!owned) {
      owned = new Object("ownedprovinces");
      owned->setObjList(true); 
      (*hoi)->setValue(owned); 
    }
    owned->clear();

    Object* controlled = (*hoi)->safeGetObject("controlledprovinces");
    if (!controlled) {
      controlled = new Object("controlledprovinces");
      controlled->setObjList(true); 
      (*hoi)->setValue(controlled); 
    }
    controlled->clear();
  }

  objvec toBeFixed; 
  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    if (!(*hoi)->safeGetObject("ic")) continue; 
    
    objvec vics = hoiProvinceToVicProvincesMap[*hoi];
    map<Object*, double> weights;
    pair<Object*, double> best(0, 0); 
    for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
      Object* vicCountry = findVicCountryByVicTag((*vic)->safeGetString("owner", "NONE")); 
      if (!vicCountry) {
	// Ok, this will sometimes happen with early Vicky saves. Just leave it for now. 
	continue;
      }
      weights[vicCountry] += provinceWeight(*vic);
      if (weights[vicCountry] < best.second) continue;
      best.second = weights[vicCountry];
      best.first = vicCountry; 
    }

    if (!best.first) {
      // Leave until everything that can be converted straight up is done. 
      toBeFixed.push_back(*hoi); 
      continue; 
    }
    addProvinceToVicNation((*hoi), best.first);
  }


  // Anything for which the relevant Vicky provinces were unowned: Convert as belonging to closest nation. 
  for (objiter fixit = toBeFixed.begin(); fixit != toBeFixed.end(); ++fixit) {
    Object* hpiFix = findHoiProvInfoFromHoiProvince(*fixit); 

    if (!hpiFix) {
      Logger::logStream(DebugProvinces) << "Could not find info for "
					<< (*fixit)->safeGetString("id")
					<< "\n";
      //continue; 
    }
    
    double leastDist = 1e100;
    string bestTag = "NONE"; 
    for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
      if ((*hpi)->safeGetString("owner", "NONE") == "NONE") continue; 
     
      double currDist = mapInfo->distanceHoiToHoi(hpiFix, *hpi);
      if (currDist > leastDist) continue;
      leastDist = currDist;
      bestTag = (*hpi)->safeGetString("owner");
    }

    Object* hoiOwner = findHoiCountryByHoiTag(bestTag);
    
    if (!hoiOwner) {
      Logger::logStream(Logger::Error) << "Serious problem: Could not find a HoI owner for province "
				       << (*fixit)->safeGetString("id")
				       << ", best tag is "
				       << bestTag
				       << ".\n";
      continue; 
    }

    addProvinceToHoiNation((*fixit), hoiOwner); 
  }
}

void WorkerThread::calculateProvinceWeight (Object* vicProv,
					    const vector<string>& goods,
					    const vector<string>& bonusTypes, 
					    Object* weightObject,
					    map<string, double>& totalWeights) {
  bool debug = (vicProv->safeGetString("debug", "no") == "yes"); 
  if (debug) Logger::logStream(DebugResources) << "Getting weights for province " << vicProv->getKey() << ":\n"; 
  Object* state = vicProvinceToVicStateMap[vicProv];

  double factoryWorkers = 0; 
  objvec weights = weightObject->getLeaves();
  Object* vicOwner = findVicCountryByVicTag(vicProv->safeGetString("owner"));
  static Object* dummyCountry = new Object("dummy"); 
  if (!vicOwner) vicOwner = dummyCountry; 
  objvec leaves = vicProv->getLeaves();
  objvec workers;
  objvec indWorkers;
  objvec otherPops; 
  double totalNonIcWeight = 0;
  double totalIndustrialWorkers = 0;
  double industryIC = 0;
  double industryOther = 0; 
  for (objiter pop = leaves.begin(); pop != leaves.end(); ++pop) {
    if (0 == (*pop)->safeGetObject("issues")) continue;
    if (debug) Logger::logStream(DebugResources) << "  Considering " << (*pop)->getKey() << " " << (*pop)->safeGetString("id") << "\n"; 
    if ((*pop)->getKey() == "bureaucrats") state->resetLeaf("totalBureaucrats", state->safeGetFloat("totalBureaucrats") + (*pop)->safeGetFloat("size")); 
    if (((*pop)->getKey() == "craftsmen") || ((*pop)->getKey() == "clerks")) {
      factoryWorkers += (*pop)->safeGetFloat("employed");
      totalIndustrialWorkers += (*pop)->safeGetFloat("size"); 
      indWorkers.push_back(*pop); 
    }
    else if (((*pop)->getKey() == "labourers") || ((*pop)->getKey() == "farmers")) workers.push_back(*pop);
    else if ((*pop)->getKey() != "artisans") otherPops.push_back(*pop); 
    
    string artisanProduction = (*pop)->safeGetString("production_type", "NOTHING");
    if (artisanProduction != "NOTHING") {
      artisanProduction = remQuotes(artisanProduction);
      artisanProduction = artisanProduction.substr(artisanProduction.find('_') + 1);
      (*pop)->resetLeaf(artisanProduction, (*pop)->safeGetString("size"));
      (*pop)->resetLeaf(artisanProduction + "_count", (*pop)->safeGetString("size"));
      (*pop)->resetLeaf("employed", (*pop)->safeGetString("size")); 
      
      if (debug) {
	Logger::logStream(DebugResources) << "    Artisan production weight for "
					  << artisanProduction
					  << " set to "
					  << (*pop)->safeGetString("size")
					  << "\n"; 
	
      }
    }

    
    for (objiter weight = weights.begin(); weight != weights.end(); ++weight) { // Loop over all Vic goods
      double popProduction = (*pop)->safeGetFloat((*weight)->getKey()); 
      if (0 == popProduction) continue;
      if ((*pop)->getKey() == "clerks") popProduction *= configObject->safeGetFloat("clerkBonus");
      else if ((*pop)->getKey() == "artisans") popProduction *= configObject->safeGetFloat("artisanPenalty");
      if (debug) Logger::logStream(DebugResources) << "    Production of " << (*weight)->getKey() << " is "
						   << popProduction 
						   << ", giving weights\n";
      vicOwner->resetLeaf("warIndustry", vicOwner->safeGetFloat("warIndustry") + popProduction*(*weight)->safeGetFloat("war"));
      vicOwner->resetLeaf("maxWarIndustry", vicOwner->safeGetFloat("maxWarIndustry") + popProduction);

      for (vector<string>::const_iterator i = goods.begin(); i != goods.end(); ++i) {
	double cWeight = (*weight)->safeGetFloat(*i);
	double productionToUse = popProduction;
	if ((*i) == "manpower") productionToUse = (*pop)->safeGetFloat((*weight)->getKey() + "_count"); 
	if (debug) Logger::logStream(DebugResources) << "      " << (*i) << " : "
						     << cWeight << " * " << productionToUse << " = "
						     << cWeight*productionToUse << "\n";
	cWeight *= productionToUse;
	totalWeights[*i] += cWeight;
	if ((*i) != "industry") {
	  if (((*pop)->getKey() == "clerks") || ((*pop)->getKey() == "craftsmen")) industryOther += cWeight;
	  if ((*i) != "manpower") totalNonIcWeight += cWeight;
	}
	else if (((*pop)->getKey() == "clerks") || ((*pop)->getKey() == "craftsmen")) industryIC += cWeight; 
	vicProv->resetLeaf((*i), vicProv->safeGetFloat(*i) + cWeight); 
      }

      for (vector<string>::const_iterator i = bonusTypes.begin(); i != bonusTypes.end(); ++i) {
	double cWeight = (*weight)->safeGetFloat(*i);
	if (debug) Logger::logStream(DebugResources) << "      " << (*i) << " : "
						     << cWeight << " * " << popProduction << " = "
						     << cWeight*popProduction << "\n";
	cWeight *= popProduction;
	vicOwner->resetLeaf((*i), vicOwner->safeGetFloat(*i) + cWeight);
      }
    }
  }

  
  for (objiter pop = otherPops.begin(); pop != otherPops.end(); ++pop) {
    double weight = (*pop)->safeGetFloat("size");
    totalWeights["manpower"] += weight;
    vicProv->resetLeaf("manpower", weight + vicProv->safeGetFloat("manpower")); 
  }
  
  for (objiter pop = workers.begin(); pop != workers.end(); ++pop) {
    Object* unempWeights = (*pop)->safeGetObject("unemployed");
    if (!unempWeights) {
      unempWeights = new Object("unemployed");
      (*pop)->setValue(unempWeights); 
    }
    double unemployed = (*pop)->safeGetFloat("size");
    unemployed -= (*pop)->safeGetFloat("employed");
    if (unemployed < 1) continue;
    //if (0.001 > totalNonIcWeight) break; 
    
    for (vector<string>::const_iterator i = goods.begin() + 2; i != goods.end(); ++i) {
      double cWeight = vicProv->safeGetFloat(*i) / (0.001 + totalNonIcWeight);
      cWeight *= unemployed; 
      unempWeights->resetLeaf((*i), unempWeights->safeGetFloat(*i) + cWeight);
      totalWeights[*i] += cWeight;

      if (isnan(totalWeights[*i])) Logger::logStream(Logger::Error) << "Major problem: NaN in resource "
								    << (*i)
								    << " for province "
								    << vicProv->getKey() << " "
								    << totalNonIcWeight
								    << "\n"; 
    }
  }

  if (debug) Logger::logStream(DebugResources) << "  Searching for unemployed "
					       << state->safeGetFloat("totalFactoryCapacity") << " "
					       << state->safeGetFloat("totalFactoryWorkers") << " "
					       << totalIndustrialWorkers 
					       << "\n"; 

  industryIC /= (1 + industryIC + industryOther); 
  for (objiter pop = indWorkers.begin(); pop != indWorkers.end(); ++pop) {
    Object* unempWeights = (*pop)->safeGetObject("unemployed");
    if (!unempWeights) {
      unempWeights = new Object("unemployed");
      (*pop)->setValue(unempWeights); 
    }
    double unemployed = (*pop)->safeGetFloat("size");
    unemployed -= (*pop)->safeGetFloat("employed");
    double remainingFactoryCapacity = state->safeGetFloat("totalFactoryCapacity");
    remainingFactoryCapacity -= state->safeGetFloat("totalFactoryWorkers"); 
    if (unemployed > remainingFactoryCapacity) unemployed = remainingFactoryCapacity;

    if (unemployed < 1) continue; 

    if (debug) Logger::logStream(DebugResources) << "  Unemployed in "
						 << (*pop)->getKey() << " " << (*pop)->safeGetString("id")
						 << (*pop)->safeGetFloat("size") << " - "
						 << (*pop)->safeGetFloat("employed") << " = "
						 << unemployed << " (max "
						 << remainingFactoryCapacity
						 << ")\n"; 
    
    state->resetLeaf("totalFactoryWorkers", unemployed + state->safeGetFloat("totalFactoryWorkers")); 
    unempWeights->resetLeaf("industry", unempWeights->safeGetFloat("industry") + unemployed * industryIC);
    totalWeights["industry"] += unemployed * industryIC;

    totalWeights["manpower"] += unemployed * (1 - industryIC);
    vicProv->resetLeaf("manpower", unemployed * (1 - industryIC) + vicProv->safeGetFloat("manpower")); 
  }
  
  state->resetLeaf("industrialisation", state->safeGetFloat("industrialisation") + factoryWorkers); 
  
  if (debug) {
    Logger::logStream(DebugResources) << "  Overall:\n"; 
    for (vector<string>::const_iterator i = goods.begin(); i != goods.end(); ++i) {
      Logger::logStream(DebugResources) << "    " << (*i) << " : "
					<< totalWeights[*i] << "\n"; 
    }
  }
}

Object* hasMoreIndustry (Object* hoi1, Object* hoi2) {
  int mpic1 = hoi1->safeGetInt("industry") + hoi1->safeGetInt("manpower");
  int mpic2 = hoi2->safeGetInt("industry") + hoi2->safeGetInt("manpower");
  if (mpic1 > mpic2) return hoi1;
  if (mpic1 < mpic2) return hoi2;

  // Energy tiebreaker
  mpic1 = hoi1->safeGetInt("energy");
  mpic2 = hoi2->safeGetInt("energy");
  if (mpic1 > mpic2) return hoi1;
  if (mpic1 < mpic2) return hoi2;

  // Industry tiebreaker
  mpic1 = hoi1->safeGetInt("industry");
  mpic2 = hoi2->safeGetInt("industry");
  if (mpic1 > mpic2) return hoi1;
  if (mpic1 < mpic2) return hoi2;

  // Metal tiebreaker 
  mpic1 = hoi1->safeGetInt("metal"); 
  mpic2 = hoi2->safeGetInt("metal"); 
  if (mpic1 > mpic2) return hoi1;
  return hoi2; 
}

string getActionString (string resource) {
  if (resource == "manpower") return "\"Put them in uniform.\"";
  if (resource == "oil") return "\"Drill, baby, drill!\"";
  if (resource == "energy") return "\"Time you were off to the anthracite.\"";
  if (resource == "metal") return "\"Iron! Cold Iron is master of men all.\"";
  if (resource == "rares") return "\"Pretty... glowing... rocks...\"";
  if (resource == "industry") return "\"Feed the guns!\"";
  
  return "\"Whatever.\"";
}

Object* makeCommand (int amount, string resource, string provId) {
  Object* command = new Object("command");
  if (resource == "manpower") {
    command->setLeaf("type", "manpowerpool");
    command->setLeaf("value", 10*amount); 
  }
  else if (resource == "industry") {
    command->setLeaf("type", "construct");
    command->setLeaf("which", "ic");
    command->setLeaf("where", provId);
    command->setLeaf("value", amount); 
  }
  else {
    command->setLeaf("type", "add_prov_resource");
    command->setLeaf("which", provId);
    command->setLeaf("value", amount); 
    command->setLeaf("where", resource == "rares" ? "rare_materials" : resource);
  }
  return command; 
}

Object* makeEvent (string provId, string primary, string secondary, int year, int month, string hoitag) {
  Object* event = new Object("event");
  event->setLeaf("id", eventId++);
  event->setLeaf("random", "no");
  event->setLeaf("country", hoitag); 
  Object* trigger = new Object("trigger");
  event->setValue(trigger);
  trigger->setLeaf("random", "20");
  trigger->setLeaf("money", "100"); 
  Object* control = new Object("control");
  control->setLeaf("province", provId); 
  control->setLeaf("data", "-1");
  trigger->setValue(control);
  trigger->setLeaf("year", year);
  
  event->setLeaf("name", "\"Mobilise the Unemployed\"");
  sprintf(stringbuffer, "\"Province %s has an unemployment problem. With a small investment they can be made to do something useful.\"", provId.c_str());
  event->setLeaf("desc", stringbuffer); 
  event->setLeaf("style", "0");
  
  Object* date = new Object("date");
  date->setLeaf("day", "1");
  date->setLeaf("month", convertMonth(month));
  date->setLeaf("year", year);
  event->setValue(date);
    
  month++;
  if (month >= 13) {
    month = 1;
    year++; 
  }
  
  event->setLeaf("offset", "10");
  date = new Object("deathdate");
  event->setValue(date);
  date->setLeaf("day", "29");
  date->setLeaf("month", "december");
  date->setLeaf("year", "2099");
  
  Object* action = new Object("action_a");
  event->setValue(action);
  action->setLeaf("name", getActionString(primary));
  action->setValue(makeCommand(secondary == "none" ? 1 : 4, primary, provId));
  Object* command = new Object("command");
  action->setValue(command);
  command->setLeaf("type", "money");
  command->setLeaf("value", secondary == "none" ? "-5" : "-25");

  if (secondary != "none") {
    action = new Object("action_b");
    event->setValue(action);
    action->setLeaf("name", getActionString(secondary)); 
    action->setValue(makeCommand(3, secondary, provId)); 
    command = new Object("command");
    action->setValue(command);
    command->setLeaf("type", "money");
    command->setLeaf("value", "-25");
  }
  
  return event; 
}

void WorkerThread::generateEvents (Object* evtObject, Object* vicCountry) {
  Object* unemployed = vicCountry->safeGetObject("unemployed");
  if (!unemployed) return;
  Object* hoiCountry = findHoiCountryByVicCountry(vicCountry);
  if (!hoiCountry) return; 

  for (vector<string>::iterator i = hoiProducts.begin(); i != hoiProducts.end(); ++i) {
    int current = (int) floor(0.5 + unemployed->safeGetFloat(*i));
    unemployed->resetLeaf((*i), current);
  }

  objvec provinces;
  Object* ownedProvs = hoiCountry->safeGetObject("ownedprovinces");
  if (!ownedProvs) return;
  Object* coreProvs = hoiCountry->safeGetObject("nationalprovinces");
  if (!coreProvs) return;

  map<string, int> totalResources; 
  
  for (int i = 0; i < coreProvs->numTokens(); ++i) {
    string candidate = coreProvs->getToken(i);
    for (int j = 0; j < ownedProvs->numTokens(); ++j) {
      string own = ownedProvs->getToken(j);
      if (own != candidate) continue;
      Object* prov = findHoiProvinceFromHoiId(own);
      if (!prov) break; 
      provinces.push_back(prov); 
      for (vector<string>::iterator g = hoiProducts.begin(); g != hoiProducts.end(); ++g) {
	totalResources[*g] += prov->safeGetInt(*g); 
      }
      break; 
    }
  }

  if (0 == provinces.size()) return; 
  
  for (vector<string>::iterator i = hoiProducts.begin(); i != hoiProducts.end(); ++i) {
    int initialYear = 1936;
    int initialMonth = 6; 
    
    while (4 <= unemployed->safeGetInt(*i)) {
      unemployed->resetLeaf((*i), unemployed->safeGetInt(*i) - 4);
      string secondary = "manpower";
      if ((*i) == "manpower") {
	int indexOfOther = 2 + (rand() % 4);
	secondary = hoiProducts[indexOfOther];
      }

      string hoiProv = provinces[0]->safeGetString("id");
      if (0 < totalResources[*i]) {
	int roll = rand() % totalResources[*i];
	int count = 0;
	for (objiter prov = provinces.begin(); prov != provinces.end(); ++prov) {
	  count += (*prov)->safeGetInt(*i);
	  if (count < roll) continue;
	  hoiProv = (*prov)->safeGetString("id");
	  break; 
	}
      }
      
      evtObject->setValue(makeEvent(hoiProv, (*i), secondary, initialYear, initialMonth, hoiCountry->safeGetString("tag")));
    }
    
    for (int j = 0; j < unemployed->safeGetInt(*i); ++j) {
      string hoiProv = provinces[0]->safeGetString("id"); 
      int roll = totalResources[*i] > 0 ? rand() % totalResources[*i] : 0; 
      int count = 0;
      for (objiter prov = provinces.begin(); prov != provinces.end(); ++prov) {
	count += (*prov)->safeGetInt(*i);
	if (count < roll) continue;
	hoiProv = (*prov)->safeGetString("id");
	break; 
      }
      evtObject->setValue(makeEvent(hoiProv, (*i), "none", initialYear, initialMonth, hoiCountry->safeGetString("tag")));
    }
  }

  
}

struct IndustrialDescendingSorter {
  bool operator () (Object* one, Object* two) {
    return (hasMoreIndustry(one, two) == one); 
  }
};

void WorkerThread::moveResources () {
  map<string, double> totalGoods;
  Object* goodsWeights = configObject->safeGetObject("goodsWeights");
  
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    for (vector<string>::iterator good = hoiProducts.begin(); good != hoiProducts.end(); ++good) {
      totalGoods[(*good)] += (*hpi)->safeGetFloat((*good)); 
    }
  }

  Object* bonusObject = configObject->safeGetObject("techBonusTypes");
  objvec bons = bonusObject->getValue("bonus");
  vector<string> bonusTypes;
  for (objiter b = bons.begin(); b != bons.end(); ++b) bonusTypes.push_back((*b)->getLeaf()); 
  
  map<string, double> totalWeights;
  for (objiter prov = vicProvinces.begin(); prov != vicProvinces.end(); ++prov) {
    calculateProvinceWeight((*prov), hoiProducts, bonusTypes, goodsWeights, totalWeights);
  }
  
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    double total = 1; 
    for (vector<string>::const_iterator i = bonusTypes.begin(); i != bonusTypes.end(); ++i) {
      total += (*vic)->safeGetFloat(*i);
    }
    total *= 3;
    for (vector<string>::const_iterator i = bonusTypes.begin(); i != bonusTypes.end(); ++i) {
      (*vic)->resetLeaf((*i), (*vic)->safeGetFloat(*i) / total); 
    }
  }
  
  
  for (vector<string>::iterator good = hoiProducts.begin(); good != hoiProducts.end(); ++good) {
    map<string, double> overflow;
    string keyword = (*good);
    
    for (objiter prov = vicProvinces.begin(); prov != vicProvinces.end(); ++prov) {
      bool debug = ((*prov)->safeGetString("debug", "no") == "yes"); 
      double currWeight = (*prov)->safeGetFloat(keyword);     
      string ownerTag = (*prov)->safeGetString("owner", "NONE");
      currWeight /= totalWeights[keyword]; 
      currWeight *= totalGoods[keyword];
      Object* vicOwner = findVicCountryByVicTag(ownerTag);
      if (vicOwner) vicOwner->resetLeaf(keyword, currWeight + vicOwner->safeGetFloat(keyword)); 
      currWeight += overflow[ownerTag];
      overflow[ownerTag] = (currWeight - floor(currWeight));
      currWeight -= overflow[ownerTag];
      int amount = (int) floor(currWeight + 0.5);
      if (debug) Logger::logStream(DebugResources) << "Province "
						   << (*prov)->getKey()
						   << " has "
						   << keyword
						   << " weight "
						   << (*prov)->safeGetFloat(keyword) << " * " << totalGoods[keyword] << " / " << totalWeights[keyword] << " = "
						   << ((*prov)->safeGetFloat(keyword) / totalWeights[keyword])*totalGoods[keyword] << " " 
						   << currWeight << " "
						   << amount 
						   << "\n"; 
      
      
      for (unsigned int i = 0; i < vicProvinceToHoiProvincesMap[*prov].size(); ++i) {
	int currAmount = amount / vicProvinceToHoiProvincesMap[*prov].size();
	if (i < amount % vicProvinceToHoiProvincesMap[*prov].size()) currAmount++;
	currAmount += vicProvinceToHoiProvincesMap[*prov][i]->safeGetInt((*good));
	vicProvinceToHoiProvincesMap[*prov][i]->resetLeaf((*good), currAmount);
      }
    }
  }


  // Events for unemployed
  for (objiter prov = vicProvinces.begin(); prov != vicProvinces.end(); ++prov) {
    Object* owner = findVicCountryByVicTag((*prov)->safeGetString("owner"));
    if (!owner) continue; 

    Object* ownerUnemp = owner->safeGetObject("unemployed");
    if (!ownerUnemp) {
      ownerUnemp = new Object("unemployed");
      owner->setValue(ownerUnemp); 
    }
    
    objvec workers = (*prov)->getValue("farmers");
    if (0 == workers.size()) workers = (*prov)->getValue("labourers");
    objvec indWorkers = (*prov)->getValue("craftsmen");
    for (objiter w = indWorkers.begin(); w != indWorkers.end(); ++w) workers.push_back(*w);
    indWorkers = (*prov)->getValue("clerks");
    for (objiter w = indWorkers.begin(); w != indWorkers.end(); ++w) workers.push_back(*w);
    
    for (objiter w = workers.begin(); w != workers.end(); ++w) {
      Object* unempWeights = (*w)->safeGetObject("unemployed");
      if (!unempWeights) continue;

      for (vector<string>::const_iterator i = hoiProducts.begin(); i != hoiProducts.end(); ++i) {
	ownerUnemp->resetLeaf((*i), unempWeights->safeGetFloat(*i) + ownerUnemp->safeGetFloat(*i));
      }
    }
  }

  Object* allEventsObject = new Object("allEvents"); 

  map<string, double> totalEventResources; 
  Logger::logStream(DebugResources) << "Event-granted resources from unemployed: \n\t";
  for (vector<string>::iterator g = hoiProducts.begin(); g != hoiProducts.end(); ++g) {
    Logger::logStream(DebugResources) << (*g) << "\t";
  }
  Logger::logStream(DebugResources) << "\n";
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* unempWeights = (*vic)->safeGetObject("unemployed");
    if (!unempWeights) continue;
    
    Logger::logStream(DebugResources) << "  " << (*vic)->getKey() << "\t";
    for (vector<string>::iterator g = hoiProducts.begin(); g != hoiProducts.end(); ++g) {
      string good = (*g); 
      unempWeights->resetLeaf(good, unempWeights->safeGetFloat(good) * totalGoods[good] / totalWeights[good]); 
      Logger::logStream(DebugResources) << unempWeights->safeGetFloat(good) << "\t";
      totalEventResources[good] += unempWeights->safeGetFloat(good);
    }
    Object* hoi = findHoiCountryByVicCountry(*vic);
    if ((hoi) && (unempWeights)) hoi->resetLeaf("totalIndustry", unempWeights->safeGetInt("industry") + hoi->safeGetInt("totalIndustry")); 
    Logger::logStream(DebugResources) << "\n";
    generateEvents(allEventsObject, (*vic)); 
  }
  Logger::logStream(DebugResources) << "  Total\t";
  for (vector<string>::iterator g = hoiProducts.begin(); g != hoiProducts.end(); ++g) {
    Logger::logStream(DebugResources) << totalEventResources[*g] << "\t";
  }
  Logger::logStream(DebugResources) << "\n";
    
  ofstream writer;
  writer.open(".\\Output\\db\\events\\unemployment_events.txt");  
  Parser::topLevel = allEventsObject; 
  writer << (*allEventsObject);
  writer.close();
  
  int numUrbs = 0;
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    if (remQuotes((*hpi)->safeGetString("terrain")) != "Urban") continue;
    (*hpi)->resetLeaf("terrain", "\"Plains\"");
    numUrbs++;   
  }

  vector<int> pointValues; 
  objvec newUrbs;
  Object* extraVPs = configObject->safeGetObject("extraVPs");
  if (!extraVPs) extraVPs = new Object("extraVPs");
  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    pointValues.push_back((*hoi)->safeGetInt("points"));
    int newPoints = extraVPs->safeGetInt((*hoi)->safeGetString("id"));
    (*hoi)->resetLeaf("points", newPoints); 
    (*hoi)->unsetValue("terrain"); 
    
    newUrbs.push_back(*hoi);
  }
  sort(pointValues.begin(), pointValues.end());
  reverse(pointValues.begin(), pointValues.end());
  sort(newUrbs.begin(), newUrbs.end(), IndustrialDescendingSorter());

  for (int i = 0; i < numUrbs; ++i) {
    newUrbs[i]->resetLeaf("terrain", "urban"); 
  }
  for (unsigned int i = 0; i < pointValues.size(); ++i) {
    newUrbs[i]->resetLeaf("points", newUrbs[i]->safeGetInt("points") + pointValues[i]); 
  }
  
  map<string, map<string, double> > ownerToResourcesMap; 
  
  //ofstream writer;
  writer.open(".\\Output\\db\\Province.csv");
  writer << "Id;Name;Area;Region;Continent;Climate;Terrain;SizeModifier;AirCapacity;Infrastructure;City;Beaches;Port Allowed;Port Seazone;IC;Manpower;Oil;Metal;Energy;Rare Materials;City XPos;City YPos;Army XPos;Army YPos;Port XPos;Port YPos;Beach XPos;Beach YPos;Beach Icon;Fort XPos;Fort YPos;AA XPos;AA YPos;Counter x;Counter Y;Terrain variant;Terrain x;Terrain Y;Terrain variant;Terrain x;Terrain Y;Terrain variant;Terrain x;Terrain Y;Terrain variant;Fill coord X;Fill coord Y;;;;;;\n"; 
  writer << "0;PROV0;-;;-;-;Clear;;;100;;0;0;0;;;;;;;14976;5760;14976;5760;14976;5760;14976;5760;0;;;;;;;;;;;;;;;;;-1;-1;-1;-1;;;;\n"; 

  int numNewUrbs = 0; 
  for (objiter hpi = hoiProvInfos.begin(); hpi != hoiProvInfos.end(); ++hpi) {
    Object* hoi = findHoiProvinceFromHoiProvInfo(*hpi);
    string ownerTag = "NONE";
    if (hoi) {
      ownerTag = hoi->safeGetString("owner");
      if (hoi->safeGetString("terrain", "blah") == "urban") {
	(*hpi)->resetLeaf("terrain", "\"Urban\"");
	hoi->unsetValue("terrain"); 
	numNewUrbs++;
	/*
	Logger::logStream(DebugResources) << "Made province "
					  << (*hpi)->safeGetString("id") << "("
					  << hoi->safeGetString("industry") << ", "
					  << hoi->safeGetString("manpower") << ") urban.\n";
	*/ 
      }

      Object* hoiOwner = findHoiCountryByHoiTag(ownerTag);
      if (hoiOwner) hoiOwner->resetLeaf("totalIndustry", hoiOwner->safeGetFloat("totalIndustry") + hoi->safeGetFloat("industry"));
    }
    writer << remQuotes((*hpi)->safeGetString("beforeString"));
    writer << remQuotes((*hpi)->safeGetString("terrain"));
    writer << remQuotes((*hpi)->safeGetString("midString"));
    
    for (vector<string>::iterator good = hoiProducts.begin(); good != hoiProducts.end(); ++good) {
      if (hoi) writer << hoi->safeGetString(*good);
      else writer << "0"; 
      writer << ";";
      if (hoi) {
	ownerToResourcesMap[ownerTag][*good] += hoi->safeGetFloat(*good);
	ownerToResourcesMap["Total"][*good] += hoi->safeGetFloat(*good);
      }
    }
    writer << remQuotes((*hpi)->safeGetString("afterString")) << "\n";
  }
  if (targetVersion == ".\\AoD\\") writer << "-1;;-;-;;;; ;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;-1;-1;-1;-1\n"; 
  else writer << "-1;;-;-;;;ocean;;;100;;;;;;;;;;;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;;;;;;;;;;;-1;-1;-1;-1;;;;;;;\n";
  
  writer.close();

  Logger::logStream(DebugResources) << "Total urban provinces: " << numNewUrbs << "\n"; 
  Logger::logStream(DebugResources) << "Total resources:\n";
  Logger::logStream(DebugResources) << "\t IC \tMP \tOil \tMetal \tEnergy \tRares\n"; 
  for (map<string, map<string, double> >::iterator i = ownerToResourcesMap.begin(); i != ownerToResourcesMap.end(); ++i) {
    if ((*i).first == "") continue;
    Logger::logStream(DebugResources) << (*i).first << ":\t";
    for (vector<string>::iterator good = hoiProducts.begin(); good != hoiProducts.end(); ++good) {
      Logger::logStream(DebugResources) << (*i).second[*good] << "\t";
      assert(!isnan((*i).second[*good])); 
    }
    Logger::logStream(DebugResources) << "\n";
  }
}

double selectWantedObject (Object* country, Object* vic, objvec& teams, string customString, int minSkill, int debug = DebugTechTeams) {
  if (0 == teams.size()) return -1; 
  
  Object* desired = country->safeGetObject(customString); 
  Object* selected = 0; 
  if (desired) {
    while (0 < teams.size()) {
      Object* desiredTeams = desired->safeGetObject("team");
      if (!desiredTeams) break;
      for (int i = 0; i < desiredTeams->numTokens(); ++i) {
	int desiredTeam = atoi(desiredTeams->getToken(i).c_str());
	if (0 > desiredTeam) continue;
	if (desiredTeam >= (int) teams.size()) continue; 
	selected = teams[desiredTeam];      
	if (!selected) continue;
	teams[desiredTeam] = 0; 
	break;
      }
      
      desired->removeObject(desiredTeams);
      if (selected) break; 
    }
  }

  int numAttempts = 0; 
  while (!selected) {
    int attempt = rand() % teams.size();
    selected = teams[attempt];
    if (!selected) {
      teams[attempt] = teams.back();
      teams.pop_back();
      if (0 == teams.size()) break; 
      continue; 
    }
    
    numAttempts++;
    if ((selected->safeGetFloat("skill") < minSkill) && (numAttempts < 5000)) {
      selected = 0; 
      continue;
    }
    teams[attempt] = teams.back();
    teams.pop_back();
  }
  
  if (!selected) return -1;
  country->setValue(selected);
  double ret = selected->safeGetFloat("cost", 0.01);
  selected->unsetValue("cost");

  vector<pair<string, double> > bonuses; 
  Object* fields = selected->safeGetObject("research_types");
  if ((fields) && (vic)) {
    for (int i = 0; i < fields->numTokens(); ++i) {
      string field = fields->getToken(i);
      double bonus = vic->safeGetFloat(field);
      if (0 == bonus) continue;
      ret *= (1 - bonus);
      bonuses.push_back(pair<string, double>(field, bonus));
    }
  }
  
  Logger::logStream(debug) << "Id "
			   << selected->safeGetObject("id")->safeGetInt("id")
			   << " assigned to country "
			   << country->safeGetString("tag")
			   << " at cost "
			   << ret * country->safeGetFloat("moddingPoints") << " (";
  for (vector<pair<string, double> >::iterator b = bonuses.begin(); b != bonuses.end(); ++b) {
    if (b != bonuses.begin()) Logger::logStream(debug) << "; "; 
    Logger::logStream(debug) << (*b).first << " " << (*b).second;
  }
  Logger::logStream(debug) << ")\n";

  return ret;
}

void WorkerThread::moveCapitals () {
  for (objiter c = hoiCountries.begin(); c != hoiCountries.end(); ++c) {
    Object* vic = findVicCountryByHoiCountry(*c);
    if (!vic) continue;
    Object* vicCap = vicGame->safeGetObject(vic->safeGetString("capital"));
    if (!vicCap) continue;
    Object* hoiCap = vicProvinceToHoiProvincesMap[vicCap][0];
    (*c)->resetLeaf("capital", hoiCap->safeGetString("id"));
    hoiCap->resetLeaf("points", 1 + hoiCap->safeGetInt("points")); 
  }
}

void WorkerThread::moveTechTeams () {
  Object* contribs = configObject->safeGetObject("techTeamPoints");
  if (!contribs) {
    contribs = new Object("contribs");
    contribs->resetLeaf("clergymen", 1); 
  }

  Logger::logStream(Logger::Game) << "Starting tech-team redistribution\n";

  for (objiter c = hoiCountries.begin(); c != hoiCountries.end(); ++c) {
    Object* desired = (*c)->safeGetObject("customTechTeams"); 
    if (!desired) continue;
    objvec teams = desired->getValue("team");
    for (unsigned int i = 0; i < teams.size(); ++i) {
      for (int j = 0; j < teams[i]->numTokens(); ++j) {
	int idx = atoi(teams[i]->getToken(j).c_str());
	if (0 > idx) continue;
	if (idx >= (int) techTeams.size()) continue;
	if (!techTeams[idx]) {
	  Logger::logStream(Logger::Warning) << "Warning: Tech team "
					     << idx
					     << " wanted by "
					     << (*c)->safeGetString("tag")
					     << " does not exist.\n";
	  continue;
	} 
	techTeams[idx]->resetLeaf("cost", techTeams[idx]->safeGetFloat("cost") + pow(0.5, i));
      }
    }
  }

  for (objiter t = techTeams.begin(); t != techTeams.end(); ++t) {
    if (!(*t)) continue;
    double cost = (*t)->safeGetFloat("cost");
    if (0 == cost) {
      (*t)->resetLeaf("cost", 0.03);
      continue; 
    }
    cost = 0.05*sqrt(cost);
    if (cost > 0.5) cost = 0.5; 
    (*t)->resetLeaf("cost", cost); 
  }

  objvec orderedCountries;
  double totalModPoints = 0; 
  for (objiter c = hoiCountries.begin(); c != hoiCountries.end(); ++c) {
    Object* vic = findVicCountryByHoiCountry(*c);
    if (!vic) continue; 
    orderedCountries.push_back(*c);
    double modPoints = 0;
    ClassInfo* classes = vicCountryToClassInfoMap[vic];
    if (classes) {
      objvec points = contribs->getLeaves();
      for (objiter p = points.begin(); p != points.end(); ++p) {
	modPoints += classes->getWeightedCulture((*p)->getKey()) * contribs->safeGetFloat((*p)->getKey());
	Logger::logStream(DebugTechTeams) << "Contribution "
				 << vic->getKey() << " "
				 << (*p)->getKey() << " "
				 << classes->getWeightedCulture((*p)->getKey()) << " "
				 << contribs->safeGetFloat((*p)->getKey()) << " "
				 << "\n"; 
      }
      modPoints /= classes->getCulture("total"); 
    }
    totalModPoints += modPoints;
    (*c)->resetLeaf("moddingPoints", modPoints);
  }

  totalModPoints /= 100; 
  
  for (objiter c = orderedCountries.begin(); c != orderedCountries.end(); ++c) {
    (*c)->resetLeaf("moddingPoints", (*c)->safeGetFloat("moddingPoints") / totalModPoints);
    Logger::logStream(DebugTechTeams) << "Country " << (*c)->safeGetString("tag")
			     << " gets "
			     << (*c)->safeGetFloat("moddingPoints")
			     << " modding points.\n"; 
  }


  ObjectDescendingSorter sorter("moddingPoints"); 

  Object* maxTeams = configObject->safeGetObject("maxTechTeams");
  if (!maxTeams) {
    maxTeams = new Object("maxTeams");
    maxTeams->setLeaf("5", "3");
    maxTeams->setLeaf("10", "5");
    maxTeams->setLeaf("25", "10");
    maxTeams->setLeaf("50", "20");
    maxTeams->setLeaf("100", "30");
    maxTeams->setLeaf("200", "40");
    maxTeams->setLeaf("500", "60");
    maxTeams->setLeaf("1000", "80");
    maxTeams->setLeaf("10000", "100");
  }
  objvec levels = maxTeams->getLeaves(); 
  for (objiter curr = orderedCountries.begin(); curr != orderedCountries.end(); ++curr) {
    Object* bestVic = findVicCountryByHoiCountry(*curr);
    double industry = (*curr)->safeGetFloat("totalIndustry");
    int currUpperBound = 1000000000;
    int numTeams = 1; 
    for (objiter level = levels.begin(); level != levels.end(); ++level) {
      int candLimit = atoi((*level)->getKey().c_str());
      if (candLimit <= industry) continue;
      if (candLimit > currUpperBound) continue;
      currUpperBound = candLimit;
      numTeams = atoi((*level)->getLeaf().c_str());
    }
    bestVic->resetLeaf("maxTeams", numTeams);
    Logger::logStream(DebugTechTeams) << "Tag " << bestVic->getKey() << " " << (*curr)->safeGetString("tag") << " gets max "
				      << numTeams << " tech teams from " << industry << " IC.\n"; 
  }
  

  int minimumSkill = configObject->safeGetInt("minimumSkill", 2);
  bool teamsLeft = true; 
  while (teamsLeft) { 
    sort(orderedCountries.begin(), orderedCountries.end(), sorter);
    int assigned = 0; 
    for (objiter curr = orderedCountries.begin(); curr != orderedCountries.end(); ++curr) {
      if ((*curr)->safeGetFloat("moddingPoints") < 0.01) continue; 
      Object* bestVic = findVicCountryByHoiCountry(*curr);
      int existingTeams = bestVic->safeGetInt("gotTeams");
      if (existingTeams >= bestVic->safeGetInt("maxTeams")) continue; 
      
      double cost = selectWantedObject((*curr), bestVic, techTeams, "customTechTeams", minimumSkill);
      if (0 > cost) break; // Couldn't find a team
      (*curr)->resetLeaf("moddingPoints", (*curr)->safeGetFloat("moddingPoints") * (1 - cost));
      bestVic->resetLeaf("gotTeams", 1 + existingTeams); 
      assigned++; 
    }

    if (0 == assigned) break; 
    
    teamsLeft = false; 
    for (objiter t = techTeams.begin(); t != techTeams.end(); ++t) {
      if (!(*t)) continue;
      teamsLeft = true;
      break; 
    }
  }


  
}

void WorkerThread::moveAnyLeaders (objvec& officerRanks, string keyword, int index, vector<objvec>& theOfficers) {
  Logger::logStream(Logger::Game) << "Starting " << keyword << " redistribution\n";
  
  objvec qualityList; 
  static double* weights = 0;
  if (!weights) {
    weights = new double[3];
    weights[0] = 1;
    weights[1] = 0.5;
    weights[2] = 0.1; 
  }


  // Sort hoiCountries by total Vic officers, then qualityList by percentage,
  // for use in deciding numbers and ranks respectively. 
  for (objiter c = hoiCountries.begin(); c != hoiCountries.end(); ++c) {
    Object* vic = findVicCountryByHoiCountry(*c);
    if (!vic) continue; 
    ClassInfo* classes = vicCountryToClassInfoMap[vic];
    if (!classes) continue;

    double weighting = weights[index];
    Object* custom = (*c)->safeGetObject("customOfficers");
    if (custom) {
      objvec priorities = custom->getValue("priority");
      for (unsigned int i = 0; i < priorities.size(); ++i) {
	if (priorities[i]->getLeaf() != keyword) continue;
	weighting = weights[i];
	break; 
      }
    }
    
    (*c)->resetLeaf("moddingPoints", classes->getCulture("officers") * weighting);
    qualityList.push_back(*c); 
  }

  static ObjectDescendingSorter sorter("moddingPoints"); 
  sort(hoiCountries.begin(), hoiCountries.end(), sorter);

  for (objiter c = hoiCountries.begin(); c != hoiCountries.end(); ++c) {
    Object* vic = findVicCountryByHoiCountry(*c);
    if (!vic) continue; 
    ClassInfo* classes = vicCountryToClassInfoMap[vic];
    if (!classes) continue;

    (*c)->resetLeaf("moddingPoints", classes->getWeightedCulture("officers") / classes->getWeightedCulture("total")); 
  }

  sort(qualityList.begin(), qualityList.end(), sorter);
  for (unsigned int i = 0; i < qualityList.size(); ++i) {
    qualityList[i]->resetLeaf("qualityRank", int(i));
  }
  
  static ObjectDescendingSorter totals("total");
  sort(officerRanks.begin(), officerRanks.end(), totals);
 
  for (unsigned int i = 0; i < hoiCountries.size(); ++i) {   
    Object* hoi = hoiCountries[i];
    if (hoi->safeGetString("tag") == "REB") continue; 
    Object* vic = findVicCountryByHoiCountry(hoi);
    Object* custom = 0;
    if (vic) custom = customObject ? customObject->safeGetObject(vic->getKey()) : 0;
    if (custom) custom = custom->safeGetObject("officerNames");
    objvec kustomNames; 
    if (custom) {
      for (int c = 0; c < custom->numTokens(); ++c) {
	Object* hoiCountry = findHoiCountryByHoiTag(custom->getToken(c));
	if (!hoiCountry) continue;
	hoiCountry = hoiCountry->safeGetObject("officerNames");
	if (!hoiCountry) continue;
	objvec names = hoiCountry->getLeaves();
	for (objiter n = names.begin(); n != names.end(); ++n) kustomNames.push_back(*n); 
      }
    }
    double modrank = hoi->safeGetInt("qualityRank");
    modrank /= hoiCountries.size(); 
    
    Logger::logStream(DebugLeaders) << "Assigning " << keyword << " to " << hoi->safeGetString("tag") << "\n"; 
    Object* ranks = officerRanks[i];
    for (int rank = 0; rank < 4; ++rank) {
      sprintf(stringbuffer, "%i", rank);

      double minSkill = 10000;
      double maxSkill = 0; 
      for (objiter gen = theOfficers[rank].begin(); gen != theOfficers[rank].end(); ++gen) {
	if (!(*gen)) continue;
	double skill = (*gen)->safeGetFloat("skill");
	if (skill > maxSkill) maxSkill = skill;
	if (skill < minSkill) minSkill = skill;
      }

      double boostedSkill = minSkill + (maxSkill - minSkill)*modrank;
      double sigma = sqrt(0.5*(maxSkill - minSkill)); 
      double adjustedTotal = 0; 
      for (objiter gen = theOfficers[rank].begin(); gen != theOfficers[rank].end(); ++gen) {
	if (!(*gen)) continue;
	double skill = (*gen)->safeGetFloat("skill");
	adjustedTotal += 1 + 0.1*exp(-0.5*pow(skill - boostedSkill, 2) / sigma); 
      }
      
      int numToGet = ranks->safeGetInt(stringbuffer); 
      //Logger::logStream(DebugLeaders) << "  Rank " << stringbuffer << ", found " << numToGet << " in " << modrank
      
      int count = 0; 
      for (unsigned int g = 0; g < theOfficers[rank].size(); ++g) {
	Object* gen = theOfficers[rank][g];
	if (!gen) continue; 
	double skill = gen->safeGetFloat("skill");
	double weight = 1 + 0.1*exp(-0.5*pow(skill - boostedSkill, 2) / sigma); 
	weight *= numToGet;
	weight /= adjustedTotal;
	double roll = rand();
	roll /= RAND_MAX;
	if (roll > weight) continue;
	hoi->setValue(gen);
	if (0 < kustomNames.size()) {
	  int nameIdx = rand() % kustomNames.size();
	  gen->resetLeaf("name", kustomNames[nameIdx]->safeGetString("name"));
	  gen->resetLeaf("picture", kustomNames[nameIdx]->safeGetString("picture"));
	  kustomNames[nameIdx] = kustomNames.back();
	  kustomNames.pop_back(); 
	}
	theOfficers[rank][g] = 0;
	count++; 
      }
      Logger::logStream(DebugLeaders) << "  " << count << " of rank " << rank << ", expected " << numToGet << " (" << ranks->safeGetString("tag") << ")\n"; 
    }
  }

}

void WorkerThread::moveLeaders () {
  moveAnyLeaders(generalRanks, "generals", 0, generals);
  moveAnyLeaders(admiralRanks, "admirals", 1, admirals);
  moveAnyLeaders(commderRanks, "commanders", 2, commders);  

  for (objiter c = hoiCountries.begin(); c != hoiCountries.end(); ++c) {
    (*c)->unsetValue("qualityRank");
  }
 
}

void getRandomField (Object* min, objvec& ministers, string field, string def) {
  int idx = rand() % ministers.size();
  min->setLeaf(field, ministers[idx]->safeGetString(field, def));
}

void loopForMinisters (objvec& countries, objvec& ministers, int multiplier) {
  Logger::logStream(DebugMinisters) << "Distributing "
				    << (int) ministers.size() << " "
				    << ministers[0]->safeGetString("position")
				    << " of ideology "
				    << ministers[0]->safeGetString("category") 
				    << " to "
				    << (int) countries.size()
				    << "\n"; 

  if (1 == multiplier) {
    while (ministers.size() < 3*countries.size()) {
      Object* newMinister = new Object("minister");
      Object* id = new Object("id");
      id->setLeaf("type", "9");
      id->setLeaf("id", ++maxMinisterId); 
      newMinister->setValue(id);
      getRandomField(newMinister, ministers, "name", "\"Some Guy\"");
      getRandomField(newMinister, ministers, "picture", "\"M501009\"");
      getRandomField(newMinister, ministers, "personality", ministers[0]->safeGetString("personality"));
      getRandomField(newMinister, ministers, "category", ministers[0]->safeGetString("category"));
      getRandomField(newMinister, ministers, "position", ministers[0]->safeGetString("position"));      
      newMinister->setLeaf("cabinet", "replacement");
      newMinister->setLeaf("year", "1936");
      ministers.push_back(newMinister); 
    }
  }
  
  for (unsigned int i = 0; i < countries.size(); ++i) {
    if (0 == ministers.size()) break;
    if (i*multiplier >= countries.size()) break;
    if (countries[i]->safeGetString("tag") == "REB") continue; 
    int idx = rand() % ministers.size();
    Object* minister = ministers[idx];
    countries[i]->setValue(minister);

    if (minister->safeGetString("category") == countries[i]->safeGetString("govType")) {
      string position = minister->safeGetString("position");
      Object* cabinet = countries[i]->safeGetObject(position);
      Object* minId = minister->safeGetObject("id");
      cabinet->resetLeaf("id", minId->safeGetString("id"));
    }
    minister->resetLeaf("year", "1936"); 
    
    ministers[idx] = ministers.back();
    ministers.pop_back();
  }

}

void WorkerThread::moveMinisters () {
  Logger::logStream(Logger::Game) << "Starting minister redistribution\n";

  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {  
    Object* custom = (*hoi)->safeGetObject("customMinisters");
    if (custom) {
      double total = 0;
      for (map<string, map<string, objvec> >::iterator ideology = ministers.begin(); ideology != ministers.end(); ++ideology) {
	total += max(10.0, min(custom->safeGetFloat((*ideology).first, 1), 1.0));
      }
      for (map<string, map<string, objvec> >::iterator ideology = ministers.begin(); ideology != ministers.end(); ++ideology) {
	double curr = custom->safeGetFloat((*ideology).first, 1);
	curr /= total;
	custom->resetLeaf((*ideology).first, curr);
      }      
    }
    else {
      custom = new Object("customMinisters");
      double weight = 1.0 / ministers.size();
      for (map<string, map<string, objvec> >::iterator ideology = ministers.begin(); ideology != ministers.end(); ++ideology) {
	custom->resetLeaf((*ideology).first, weight); 
      }
      (*hoi)->setValue(custom); 
    }
  }
  
  ObjectDescendingSorter sorter("moddingPoints");
  
  for (map<string, map<string, objvec> >::iterator ideology = ministers.begin(); ideology != ministers.end(); ++ideology) {
    for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {  
      Object* custom = (*hoi)->safeGetObject("customMinisters");
      double curr = custom->safeGetFloat((*ideology).first);
      (*hoi)->resetLeaf("moddingPoints", curr); 
    }

    sort(hoiCountries.begin(), hoiCountries.end(), sorter);
    
    for (map<string, objvec>::iterator pos = (*ideology).second.begin(); pos != (*ideology).second.end(); ++pos) {     
      objvec currMinisters = (*pos).second;

      loopForMinisters(hoiCountries, currMinisters, 1); 
      if ((*pos).first == "HeadOfState") continue;
      if ((*pos).first == "HeadOfGovernment") continue; 
      loopForMinisters(hoiCountries, currMinisters, 2);
      loopForMinisters(hoiCountries, currMinisters, 3);
      loopForMinisters(hoiCountries, currMinisters, 10); 
    }
  } 
}

void setPercentiles (objvec& targets, string keyword, string newkey) {
  ObjectAscendingSorter sorter(keyword); 
  sort(targets.begin(), targets.end(), sorter);
  
  if (1 == targets.size()) {
    targets[0]->resetLeaf(newkey, "0");
    return; 
  }
  double final = targets.size();
  final--;
  for (unsigned int i = 0; i < targets.size(); ++i) {
    targets[i]->resetLeaf(newkey, i / final);
  }
}

double getMedian (vector<double>& data) {
  if (0 == data.size()) return 0; 
  sort(data.begin(), data.end());
  if (1 == data.size() % 2) return data[data.size() / 2];
  double ret = data[data.size() / 2];
  ret += data[data.size() / 2 - 1];
  return 0.5*ret; 
}

void WorkerThread::provinceStructures () {
  double totalSyntheticOil = 0;
  double totalSyntheticRares = 0;   
  double totalRadar = 0;
  double totalFlak = 0; 

  setPercentiles(vicStates, "totalBureaucrats", "p_bureaucrats");
  setPercentiles(vicStates, "industrialisation", "p_industry");

  vector<int> landFortLevels;

  Object* navBaseVeto = configObject->safeGetObject("navalBaseVeto");
  if (!navBaseVeto) navBaseVeto = new Object("navalBaseVeto"); 
  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    Object* synth = (*hoi)->safeGetObject("synthetic_oil");
    if (synth) totalSyntheticOil += synth->safeGetFloat("size");
    synth = (*hoi)->safeGetObject("synthetic_rares");
    if (synth) totalSyntheticRares += synth->safeGetFloat("size");
    synth = (*hoi)->safeGetObject("radar_station");
    if (synth) totalRadar += synth->safeGetFloat("size");

    (*hoi)->unsetValue("synthetic_oil");
    (*hoi)->unsetValue("synthetic_rares");
    (*hoi)->unsetValue("radar_station");     
    (*hoi)->unsetValue("naval_base");

    Object* ownerVetoes = navBaseVeto->safeGetObject((*hoi)->safeGetString("owner"));
    if ((ownerVetoes) && (ownerVetoes->safeGetString((*hoi)->safeGetString("id"), "yes") == "no")) continue; 
    Object* hpi = findHoiProvInfoFromHoiProvince(*hoi);
    if (!hpi) continue;
    if (hpi->safeGetString("terrain", "\"Ocean\"") == "\"Ocean\"") continue;
    if (0 == hpi->safeGetInt("port")) continue; 
    Object* hoiOwner = findHoiCountryByHoiTag((*hoi)->safeGetString("owner"));
    double hoiLevel = 0; 
    for (objiter vic = hoiProvinceToVicProvincesMap[*hoi].begin(); vic != hoiProvinceToVicProvincesMap[*hoi].end(); ++vic) {
      if (findHoiCountryByVicCountry(findVicCountryByVicTag((*vic)->safeGetString("owner"))) != hoiOwner) continue;
      Object* vicBase = (*vic)->safeGetObject("naval_base");
      if (!vicBase) continue;
      double viclevel = atof(vicBase->getToken(0).c_str());
      hoiLevel = max(hoiLevel, floor(viclevel + 4.5)); 
    }
    if (1 > hoiLevel) continue;
    hoiLevel = min(10.0, hoiLevel);
    Object* hoiBase = new Object("naval_base");
    (*hoi)->setValue(hoiBase);
    hoiBase->setLeaf("type", "naval_base");
    hoiBase->setLeaf("location", (*hoi)->safeGetString("id"));
    hoiBase->setLeaf("size", hoiLevel);
    hoiBase->setLeaf("current_size", hoiLevel);
  }

  double totalVicOilFactories = 0;
  double totalVicRadFactories = 0; 
  double totalAntiAirWeight = 0; 
  
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    totalVicOilFactories += (*vic)->safeGetFloat("synthetic_oil_factory");
    totalVicRadFactories += (*vic)->safeGetFloat("radio_factory"); 
  }

  bool useFortPriority = (configObject->safeGetString("fortConversion", "ranked") == "ranked"); 

  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    Object* lFort = (*hoi)->safeGetObject("landfort");    if (lFort)  landFortLevels.push_back((int) floor(0.5 + lFort->safeGetFloat("size")));
    
    (*hoi)->unsetValue("coastalfort");
    (*hoi)->unsetValue("landfort");
    Object* flak = (*hoi)->safeGetObject("anti_air");
    if (flak) {
      totalFlak += flak->safeGetFloat("size");
      (*hoi)->unsetValue("anti_air"); 
    }
    Object* hpi = findHoiProvInfoFromHoiProvince(*hoi);
    if (!hpi) continue;
    if (hpi->safeGetString("terrain", "\"Ocean\"") == "\"Ocean\"") continue;
       
    Object* hoiOwner = findHoiCountryByHoiTag((*hoi)->safeGetString("owner"));
    double hoiLevel = 0;

    // Calculate median of these Vic quantities, use for infrastructure. 
    vector<double> infras;
    vector<double> industries;
    vector<double> bureaucrats; 
    
    for (objiter vic = hoiProvinceToVicProvincesMap[*hoi].begin(); vic != hoiProvinceToVicProvincesMap[*hoi].end(); ++vic) {
      if (findHoiCountryByVicCountry(findVicCountryByVicTag((*vic)->safeGetString("owner"))) != hoiOwner) continue;
      infras.push_back((*vic)->safeGetFloat("infrastructure"));
      industries.push_back(vicProvinceToVicStateMap[*vic]->safeGetFloat("p_industry"));
      bureaucrats.push_back(vicProvinceToVicStateMap[*vic]->safeGetFloat("p_bureaucrats"));
    
      Object* vicBase = (*vic)->safeGetObject("fort");
      if (!vicBase) continue;
      double viclevel = atof(vicBase->getToken(0).c_str());
      hoiLevel = max(hoiLevel, floor(viclevel * 0.5));

      (*hoi)->resetLeaf("vic_synthetic_oil_factory", (*hoi)->safeGetFloat("vic_synthetic_oil_factory") + (*vic)->safeGetFloat("synthetic_oil_factory"));
      (*hoi)->resetLeaf("vic_radio_factory", (*hoi)->safeGetFloat("vic_radio_factory") + (*vic)->safeGetFloat("radio_factory"));
      (*vic)->unsetValue("synthetic_oil_factory");
      (*vic)->unsetValue("radio_factory");      
    }

    double medianInfra = getMedian(infras);
    double medianIndustry = getMedian(industries);
    double medianBureaucrats = getMedian(bureaucrats);
    double totalInfra = floor(medianInfra / 0.16) * 0.05;
    totalInfra += 0.05 * ((int) floor(medianIndustry / 0.125)); 
    totalInfra += 0.05 * ((int) floor(medianBureaucrats / 0.125));
    Logger::logStream(DebugBuildings) << "Infrastructure in "
				      << (*hoi)->safeGetString("id")
				      << " set to "
				      << floor((medianInfra + 0.001) / 0.16) * 0.05 << " + "
				      << (0.05 * ((int) floor(medianIndustry / 0.125))) << " + "
				      << (0.05 * ((int) floor(medianBureaucrats / 0.125))) << " = "
				      << totalInfra << " ("
				      << medianInfra << ", " 
				      << medianIndustry << ", "
				      << medianBureaucrats 
				      << ")\n"; 

    Object* infra = (*hoi)->safeGetObject("infra");
    if (!infra) {
      infra = new Object("infra");
      infra->setLeaf("type", "infrastructure");
      infra->setLeaf("location", (*hoi)->safeGetString("id"));
      (*hoi)->setValue(infra); 
    }
    infra->resetLeaf("size", totalInfra);
    infra->resetLeaf("current_size", totalInfra);    
    
    if (1 > hoiLevel) continue;
    (*hoi)->resetLeaf("landFortPriority", hoiLevel);
    (*hoi)->resetLeaf("antiairweight", (*hoi)->safeGetFloat("antiairweight") + hoiLevel);
    totalAntiAirWeight += hoiLevel;
    if (useFortPriority) continue; 

    Object* hoiBase = new Object("landfort");
    (*hoi)->setValue(hoiBase);
    hoiBase->setLeaf("type", "land_fort");
    hoiBase->setLeaf("location", (*hoi)->safeGetString("id"));
    hoiBase->setLeaf("size", hoiLevel);
    hoiBase->setLeaf("current_size", hoiLevel); 
    
    if (0 == hpi->safeGetInt("beaches")) continue;
    
    hoiBase = new Object("coastalfort");
    (*hoi)->setValue(hoiBase);
    hoiBase->setLeaf("type", "coastal_fort");
    hoiBase->setLeaf("location", (*hoi)->safeGetString("id"));
    hoiBase->setLeaf("size", hoiLevel);
    hoiBase->setLeaf("current_size", hoiLevel);
  }

  if (useFortPriority) {
    objvec sortedProvs; 
    for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
      if (0.1 > (*hoi)->safeGetFloat("landFortPriority")) continue; 
      Object* hpi = findHoiProvInfoFromHoiProvince(*hoi);
      if (!hpi) continue;
      string ownerTag = (*hoi)->safeGetString("owner");        
      double numEnemyNeighbours = 0.1;
      Object* region = hpi->safeGetObject("hoi_region");
      if (region) {
	objvec provs = region->getValue("province");
	for (objiter p = provs.begin(); p != provs.end(); ++p) {
	  if ((*p)->safeGetString("owner") != ownerTag) numEnemyNeighbours++; 
	}
      }
      double oldLevel = (*hoi)->safeGetFloat("landFortPriority");
      if (oldLevel < 1) continue;
      double gpMod = 1;
      Object* hoiOwner = findHoiCountryByHoiTag(ownerTag);
      if (hoiOwner) {
	int provs = hoiOwner->safeGetInt("numOwned");
	if (provs < 10) gpMod = 0.1;
	if (provs > 100) gpMod = 2; 
      }
      /*
	Logger::logStream(DebugBuildings) << "Province "
	<< (*hoi)->safeGetString("id") << " ("
	<< ownerTag << ", " << hpi->safeGetString("region")
	<< ") given land fort priority "
	<< oldLevel << " * "
	<< gpMod << " * " 
	<< numEnemyNeighbours << " = "
	<< oldLevel*numEnemyNeighbours
	<< "\n";
      */
      (*hoi)->resetLeaf("landFortPriority", numEnemyNeighbours*gpMod*oldLevel); 
      sortedProvs.push_back(*hoi); 
    }
    ObjectDescendingSorter fortSorter("landFortPriority");
    sort(sortedProvs.begin(), sortedProvs.end(), fortSorter); 
    for (unsigned int i = 0; i < landFortLevels.size(); ++i) {
      if (i >= sortedProvs.size()) break;
      Object* hoiBase = new Object("landfort");
      sortedProvs[i]->setValue(hoiBase);
      hoiBase->setLeaf("type", "land_fort");
      hoiBase->setLeaf("location", sortedProvs[i]->safeGetString("id"));
      hoiBase->setLeaf("size", (double) landFortLevels[i]);
      hoiBase->setLeaf("current_size", (double) landFortLevels[i]); 

      Object* hpi = findHoiProvInfoFromHoiProvince(sortedProvs[i]);
      if (!hpi) continue;
      if (0 == hpi->safeGetInt("beaches")) continue;

      hoiBase = new Object("coastalfort");
      sortedProvs[i]->setValue(hoiBase);
      hoiBase->setLeaf("type", "coastal_fort");
      hoiBase->setLeaf("location", sortedProvs[i]->safeGetString("id"));
      hoiBase->setLeaf("size", (double) landFortLevels[i]);
      hoiBase->setLeaf("current_size", (double) landFortLevels[i]);
    }
  }
  
  map<Object*, double> radarOverflow;
  map<Object*, double> synthOverflow;
  map<Object*, double> raresOverflow; 
  map<Object*, double> aantiOverflow; 
  
  double totalAirbaseLevel = 0;
  for (objiter hoi = hoiProvinces.begin(); hoi != hoiProvinces.end(); ++hoi) {
    Object* hoiOwner = findHoiCountryByHoiTag((*hoi)->safeGetString("owner"));
    double level = totalSyntheticOil * (*hoi)->safeGetFloat("vic_synthetic_oil_factory") / totalVicOilFactories;
    level += synthOverflow[hoiOwner];
    synthOverflow[hoiOwner] = level - floor(level);
    level = floor(level);
    if (level > 0.1) {
      Object* synth = new Object("synthetic_oil");
      synth->setLeaf("type", "synthetic_oil");
      synth->setLeaf("location", (*hoi)->safeGetString("id"));
      synth->setLeaf("size", level);
      synth->setLeaf("current_size", level);

      synth = new Object("synthetic_rares");
      synth->setLeaf("type", "synthetic_rares");
      synth->setLeaf("location", (*hoi)->safeGetString("id"));
      synth->setLeaf("size", level);
      synth->setLeaf("current_size", level);      
    }

    level = (*hoi)->safeGetFloat("antiairweight") * totalFlak / totalAntiAirWeight;
    level += aantiOverflow[hoiOwner];
    aantiOverflow[hoiOwner] = level - floor(level);
    level = floor(level);
    if (level > 0.1) {

      Object* hoiBase = hoiOwner->safeGetObject("province_development");
      if (!hoiBase) {
	hoiBase = new Object("province_development");
	hoiOwner->setValue(hoiBase);
	Object* baseid = new Object("id");
	baseid->setLeaf("type", "4712");
	baseid->setLeaf("id", forty712s++);
	hoiBase->setValue(baseid);
	hoiBase->setLeaf("name", "Anti-Air Guns"); 
	hoiBase->setLeaf("location", "0"); 
	hoiBase->setLeaf("cost", "0.000");
	hoiBase->setLeaf("manpower", "0.500"); 
	hoiBase->setLeaf("type", "flak");
      }
      hoiBase->resetLeaf("state", 1 + hoiBase->safeGetInt("state"));
      hoiBase->resetLeaf("size", 1 + hoiBase->safeGetInt("size"));
      hoiBase->resetLeaf("done", 1 + hoiBase->safeGetInt("done"));
    }
    
    level = totalSyntheticRares * (*hoi)->safeGetFloat("vic_synthetic_oil_factory") / totalVicOilFactories;
    level += raresOverflow[hoiOwner];
    raresOverflow[hoiOwner] = level - floor(level);
    level = floor(level);
    if (level > 0.1) {
      Object* synth = new Object("synthetic_rares");
      synth->setLeaf("type", "synthetic_rares");
      synth->setLeaf("location", (*hoi)->safeGetString("id"));
      synth->setLeaf("size", level);
      synth->setLeaf("current_size", level);      
    }

    level = totalRadar * (*hoi)->safeGetFloat("vic_radio_factory") / totalVicRadFactories;
    level += radarOverflow[hoiOwner];
    radarOverflow[hoiOwner] = level - floor(level);
    level = floor(level);
    if (level > 0.1) {
      Object* synth = new Object("radar_station");
      synth->setLeaf("type", "radar_station");
      synth->setLeaf("location", (*hoi)->safeGetString("id"));
      synth->setLeaf("size", level);
      synth->setLeaf("current_size", level);      
    }

    
    Object* hoiBase = (*hoi)->safeGetObject("air_base");
    if (!hoiBase) continue;
    totalAirbaseLevel += hoiBase->safeGetFloat("size");
    (*hoi)->unsetValue("air_base");
  }

  Logger::logStream(DebugBuildings) << "Total HoI airbases: " << totalAirbaseLevel << "\n"; 
  
  double totalVicWeight = 0;
  for (objiter vc = vicCountries.begin(); vc != vicCountries.end(); ++vc) {
    objvec armies = (*vc)->getValue("army");
    bool hasPlanes = false; 
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regs = (*army)->getValue("regiment");
      for (objiter reg = regs.begin(); reg != regs.end(); ++reg) {
	string regtype = (*reg)->safeGetString("type", "NONE");
	if (regtype != "plane") continue;
	Object* vicLocation = vicGame->safeGetObject((*army)->safeGetString("location"));
	if (!vicLocation) continue;
	if ((*vc)->getKey() != remQuotes(vicLocation->safeGetString("owner"))) continue;
	totalVicWeight++;
	vicLocation->resetLeaf("airBaseWeight", 1 + vicLocation->safeGetInt("airBaseWeight"));
	hasPlanes = true; 
      }
    }
    if (hasPlanes) {
      Object* vicCap = vicGame->safeGetObject((*vc)->safeGetString("capital"));
      if (vicCap) {
	vicCap->resetLeaf("airBaseWeight", 1 + vicCap->safeGetInt("airBaseWeight"));
	totalVicWeight++; 
      }
    }

    objvec states = (*vc)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      Object* prov = (*state)->safeGetObject("provinces");
      if (!prov) continue;      
      if (0 == prov->numTokens()) continue;
      prov = vicGame->safeGetObject(prov->getToken(0));
      if (!prov) continue;
      
      objvec buildings = (*state)->getValue("state_buildings");
      double level = 0;       
      for (objiter build = buildings.begin(); build != buildings.end(); ++build) {
	if (remQuotes((*build)->safeGetString("building")) != "aeroplane_factory") continue;
	Object* empment = (*build)->safeGetObject("employment");
	if (!empment) continue;
	Object* empees = empment->safeGetObject("employees");
	if (!empees) continue;
	objvec emps = empees->getLeaves(); 
	for (objiter emp = emps.begin(); emp != emps.end(); ++emp) {
	  level += (*emp)->safeGetFloat("count"); 
	}
      }
      level /= 10000;
      prov->resetLeaf("airBaseWeight", level + prov->safeGetFloat("airBaseWeight"));
      totalVicWeight += level;
    }
  }

  Logger::logStream(DebugBuildings) << "Vic airbase weight: " << totalVicWeight << "\n";

  map<string, double> overflow;
  map<string, double> existing; 
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    double weight = (*vic)->safeGetFloat("airBaseWeight");
    if (0.1 > weight) continue;
    Logger::logStream(DebugBuildings) << "Airbase weight of province "
				     << (*vic)->getKey()
				     << " is "
				     << weight
				     << "\n";     
    Object* hoi = 0;
    for (objiter cand = vicProvinceToHoiProvincesMap[*vic].begin(); cand != vicProvinceToHoiProvincesMap[*vic].end(); ++cand) {
      if (findVicTagFromHoiTag((*cand)->safeGetString("owner")) != remQuotes((*vic)->safeGetString("owner"))) continue;
      hoi = (*cand);
      break; 
    }
    if (!hoi) continue;
    Object* hpi = findHoiProvInfoFromHoiProvince(hoi);
    if (!hpi) continue;
    if (hpi->safeGetString("terrain", "\"Ocean\"") == "\"Ocean\"") continue;

    string ownerTag = (*vic)->safeGetString("owner"); 
    double basesize = weight / totalVicWeight;
    basesize *= totalAirbaseLevel;
    basesize += overflow[ownerTag];
    overflow[ownerTag] = basesize - floor(basesize);
    basesize = floor(basesize);
    if (1 > basesize) continue;
    if (basesize < existing[ownerTag]) {
      overflow[ownerTag] += basesize;
      continue; 
    }
    
    Object* airbase = hoi->safeGetObject("air_base");
    if (!airbase) {
      airbase = new Object("air_base");
      airbase->setLeaf("type", "air_base");
      airbase->setLeaf("location", hoi->safeGetString("id"));
      hoi->setValue(airbase); 
    }
    basesize += airbase->safeGetFloat("size");
    if (basesize > 10) {
      overflow[ownerTag] += (basesize - 10);
      basesize = 10; 
    }
    airbase->resetLeaf("size", basesize);
    airbase->resetLeaf("current_size", basesize);
    existing[ownerTag] = min(basesize, 5.0);
  }  
}

struct RevoltInfo {
  objvec provinces;
  int totalSize;
  string culture;
  map<Object*, int> vicOwners; 
};

void WorkerThread::revolters () {
  Logger::logStream(Logger::Game) << "Starting revolter creation\n";
  Object* revoltFile = loadTextFile(targetVersion + "revolt.txt");
  objvec possibleTags = revoltFile->getLeaves(); 
  map<string, RevoltInfo*> revolts; 
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    Object* vicOwner = findVicCountryByVicTag((*vic)->safeGetString("owner"));
    if (!vicOwner) continue; 

    map<string, int> cultureSizes; 
    objvec leaves = (*vic)->getLeaves();
    int accepted = 0;
    int maxNot = 0;
    string bestNot;
    for (objiter pop = leaves.begin(); pop != leaves.end(); ++pop) {
      Object* issues = (*pop)->safeGetObject("issues");
      if (!issues) continue;

      if ((*pop)->safeGetString("acceptedCulture", "no") == "yes") {
	accepted += (*pop)->safeGetInt("size");
      }
      else {
	pair<string, string> cultrel = extractCulture(*pop);
	cultureSizes[cultrel.first] += (*pop)->safeGetInt("size");
	if (maxNot < cultureSizes[cultrel.first]) {
	  maxNot = cultureSizes[cultrel.first];
	  bestNot = cultrel.first; 
	}
      }
    }
    if (maxNot < accepted) continue;
    RevoltInfo* curr = revolts[bestNot];
    if (!curr) {
      curr = new RevoltInfo();
      curr->culture = bestNot;       
      revolts[bestNot] = curr;
    }
    curr->provinces.push_back(*vic);
    curr->totalSize += maxNot;
    curr->vicOwners[vicOwner]++; 
  }

  while (true) {
    if (0 == possibleTags.size()) {
      Logger::logStream(DebugRevolters) << "No more available tags, ending.\n";
      break;
    }
    
    RevoltInfo* largest = 0; 
    for (map<string, RevoltInfo*>::iterator i = revolts.begin(); i != revolts.end(); ++i) {
      if (0 == (*i).second) continue;
      if ((0 == largest) || ((*i).second->totalSize > largest->totalSize)) largest = (*i).second; 
    }
    if (0 == largest) {
      Logger::logStream(DebugRevolters) << "No more minority cultures, ending.\n";
      break;
    }
    revolts[largest->culture] = 0;

    Object* revolter = 0; 
    for (unsigned int i = 0; i < possibleTags.size(); ++i) {
      Object* curr = possibleTags[i];                
      string tag = curr->getKey();
      Object* hoi = findHoiCountryByHoiTag(tag);
      if (hoi) {
	Object* owned = hoi->safeGetObject("ownedprovinces");
	if (0 != owned->numTokens()) {
	  // Can't use this, it's an existing nation.
	  possibleTags[i] = possibleTags.back();
	  possibleTags.pop_back();
	  --i; 
	  continue; 
	}
      }

      revolter = curr; 
      possibleTags[i] = possibleTags.back();
      possibleTags.pop_back();
      break; 
    }

    Logger::logStream(DebugRevolters) << "Using tag "
				      << revolter->getKey() << " for revolt of culture "
				      << largest->culture
				      << "\n"; 

    int maxOwned = 0;
    Object* mainVicNation = 0; 
    for (map<Object*, int>::iterator i = largest->vicOwners.begin(); i != largest->vicOwners.end(); ++i) {
      if ((*i).second < maxOwned) continue;
      maxOwned = (*i).second;
      mainVicNation = (*i).first;
    }
    assert(mainVicNation);
    Object* mainHoiNation = findHoiCountryByVicCountry(mainVicNation); 
    
    Object* expiry = revolter->safeGetObject("expirydate");
    if (!expiry) {
      expiry = new Object("expirydate");
      expiry->setLeaf("day", "1");
      expiry->setLeaf("month", "january");
      revolter->setValue(expiry);
    }
    expiry->resetLeaf("year", "1999");

    vector<string> needed;
    vector<string> surplus;
    Object* minimum = revolter->safeGetObject("minimum");
    if (!minimum) {
      minimum = new Object("minimum");
      revolter->setValue(minimum);
      minimum->setObjList(true);
    }
    minimum->clear(); 
    Object* extra = revolter->safeGetObject("extra");
    if (!extra) {
      extra = new Object("extra");
      revolter->setValue(extra);
      extra->setObjList(true);
    }
    extra->clear(); 
    
    for (objiter vp = largest->provinces.begin(); vp != largest->provinces.end(); ++vp) {
      for (objiter hp = vicProvinceToHoiProvincesMap[*vp].begin(); hp != vicProvinceToHoiProvincesMap[*vp].end(); ++hp) {
	string hoiId = (*hp)->safeGetString("id");
	if (find(needed.begin(), needed.end(), hoiId) != needed.end()) continue;
	if (find(surplus.begin(), surplus.end(), hoiId) != surplus.end()) continue;

	Object* hoiOwner = findHoiCountryByHoiTag((*hp)->safeGetString("owner"));
	if (!hoiOwner) continue;
	if (hoiOwner == mainHoiNation) {
	  needed.push_back(hoiId);
	  minimum->addToList(hoiId);
	}
	else {
	  surplus.push_back(hoiId);
	  extra->addToList(hoiId); 
	}
      }
    }
    if (minimum->numTokens() > 0) revolter->resetLeaf("capital", minimum->getToken(0));
    else revolter->unsetValue("capital"); 
  }

  ofstream writer;
  writer.open(".\\Output\\db\\revolt.txt");  
  Parser::topLevel = revoltFile; 
  writer << (*revoltFile);
  writer.close();

  Logger::logStream(Logger::Game) << "Done with revolters\n"; 
}

void WorkerThread::sliders () {
  Object* slConfig = configObject->safeGetObject("sliders");
  if (!slConfig) slConfig = new Object("sliders"); 

  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    (*vic)->resetLeaf("warIndustry", (*vic)->safeGetFloat("warIndustry") / (*vic)->safeGetFloat("maxWarIndustry", 1e20)); 
  }
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    if ((*hoi)->safeGetString("tag") == "REB") continue;
    Object* policy = (*hoi)->safeGetObject("policy");
    if (!policy) continue;
    Object* vic = findVicCountryByHoiCountry(*hoi);
    if (!vic) continue;

    Logger::logStream(DebugSliders) << "Setting sliders for HoI tag " << (*hoi)->safeGetString("tag") << "\n"; 
    
    objvec sliders = policy->getLeaves();
    for (objiter slider = sliders.begin(); slider != sliders.end(); ++slider) {
      if ((*slider)->getKey() == "date") continue;

      Object* config = slConfig->safeGetObject((*slider)->getKey());
      if (!config) {
	policy->resetLeaf((*slider)->getKey(), 5);
	Logger::logStream(DebugSliders) << "  No information for " << (*slider)->getKey() << ", setting to 5.\n"; 
	continue; 
      }

      bool centrify = false; 
      double value = config->safeGetFloat("base", 5);
      Logger::logStream(DebugSliders) << "  Initial " << (*slider)->getKey() << " value " << value << "\n"; 
      objvec keywords = config->getValue("keyword");
      for (objiter key = keywords.begin(); key != keywords.end(); ++key) {
	string which = (*key)->safeGetString("which", "BLAH");
	string vicValue = vic->safeGetString(which, "NOTHING");
	string center = (*key)->safeGetString(vicValue, "NOTHING");
	if (center == "center") {
	  centrify = true;
	  Logger::logStream(DebugSliders) << "  Setting moderation true due to " << which << " of " << vicValue << "\n"; 
	}
	else {
	  value += (*key)->safeGetFloat(vicValue);
	  Logger::logStream(DebugSliders) << "  Modifying by " << (*key)->safeGetFloat(vicValue)
					  << " due to " << which << " value " << vicValue << "\n"; 
	}
      }

      objvec quantities = config->getValue("quantity");
      for (objiter quant = quantities.begin(); quant != quantities.end(); ++quant) {
	string which = (*quant)->safeGetString("which", "BLAH");
	double vicValue = vic->safeGetFloat(which, 0);
	if (which == "voters") {
	  VoterInfo* voteInfo = vicCountryToVoterInfoMap[vic];
	  if (voteInfo) vicValue = voteInfo->getIssuePercentage((*quant)->safeGetInt("issue")); 
	}
	else if (which == "class") {
	  ClassInfo* classInfo = vicCountryToClassInfoMap[vic];
	  if (classInfo) vicValue = classInfo->getWeightedCulture((*quant)->safeGetString("type")) / classInfo->getWeightedCulture("total"); 
	}
	
	vicValue -= (*quant)->safeGetFloat("base");
	Logger::logStream(DebugSliders) << "  Adjusting by " << (*quant)->safeGetFloat("mult") * vicValue
					<< " due to " << which << " value " << vicValue << " from base " << (*quant)->safeGetFloat("base") << "\n"; 
	value += (*quant)->safeGetFloat("mult") * vicValue; 
      }
      
      if (centrify) {
	Logger::logStream(DebugSliders) << "  Moderating from " << value << " to " << 0.5*(value + 5) << "\n"; 
	value = 0.5*(value + 5);
      }
      
      if (value < 1) value = 1;
      if (value > 10) value = 10;
      int finalValue = (int) floor(value+0.5);
      Logger::logStream(DebugSliders) << "  Final result: " << finalValue << "\n"; 
      policy->resetLeaf((*slider)->getKey(), finalValue);
    }
  }
}

void distribute (objvec& hoiCountries, string resource, string keyword, bool isInt = false, bool debug = false) {
  double totalResource = 0;
  double totalWeight = 0; 
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    totalResource += (*hoi)->safeGetFloat(resource);
    totalWeight   += (*hoi)->safeGetFloat(keyword);
  }

  if (debug) Logger::logStream(DebugStockpiles) << "Total " << resource << ", " << keyword << " : " << totalResource << ", " << totalWeight << "\n"; 
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    double curr = (*hoi)->safeGetFloat(keyword);
    if (debug) Logger::logStream(DebugStockpiles) << "  " << (*hoi)->safeGetString("tag") << ": " << curr; 
    curr *= totalResource;
    curr /= totalWeight;
    if (debug) Logger::logStream(DebugStockpiles) << " " << curr << "\n"; 
    if (isInt) (*hoi)->resetLeaf(resource, (int) floor(curr + 0.5));
    else (*hoi)->resetLeaf(resource, curr); 
    (*hoi)->unsetValue(keyword); 
  }
}

void WorkerThread::stockpiles () {
  // Oil, supplies, rares, metal, energy, money
  
  for (objiter prov = vicProvinces.begin(); prov != vicProvinces.end(); ++prov) {
    Object* vicOwner = findVicCountryByVicTag((*prov)->safeGetString("owner"));
    if (!vicOwner) continue;
    Object* hoiOwner = findHoiCountryByVicCountry(vicOwner);
    if (!hoiOwner) continue;
    
    //objvec soldiers = (*prov)->getValue("soldiers");
    objvec soldiers = (*prov)->getLeaves(); 
    double men = 0;
    int regiments = 0;
    double totalPop = 0;
    for (objiter s = soldiers.begin(); s != soldiers.end(); ++s) {
      if ((*s)->getKey() == "soldiers") {
	men += (*s)->safeGetFloat("size");
	regiments += (*s)->safeGetInt("supportedRegiments");
      }
      else {
	if (!(*s)->safeGetObject("issues")) continue;
	if (!(*s)->safeGetObject("ideology")) continue;
	totalPop += (*s)->safeGetFloat("size");
      }
    }
    hoiOwner->resetLeaf("vic_manpower", men + hoiOwner->safeGetFloat("vic_manpower"));
    hoiOwner->resetLeaf("vic_regiments", regiments + hoiOwner->safeGetInt("vic_regiments"));
    hoiOwner->resetLeaf("totalPop", totalPop); 
  }

  double supportAmount = configObject->safeGetFloat("sizePerRegiment", 3000); 
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    double vmp = (*hoi)->safeGetFloat("vic_manpower");
    vmp -= supportAmount * (*hoi)->safeGetInt("vic_regiments");
    if (vmp < 0) {
      Logger::logStream(DebugStockpiles) << "Negative manpower in " << (*hoi)->safeGetString("tag")
					 << " due to " << (*hoi)->safeGetFloat("vic_manpower")
					 << " soldiers supporting " << (*hoi)->safeGetInt("vic_regiments")
					 << " regiments. Setting to 1000.\n";
      vmp = 1000;
    }
    (*hoi)->resetLeaf("vic_manpower", vmp);
    (*hoi)->unsetValue("vic_regiments"); 
  }
  
  Logger::logStream(DebugStockpiles) << "Transports and escorts: \n";
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* hoi = findHoiCountryByVicCountry(*vic);
    if (!hoi) continue;

    double money = (*vic)->safeGetFloat("money");
    objvec creds = (*vic)->getValue("creditor");
    for (objiter cred = creds.begin(); cred != creds.end(); ++cred) {
      money -= (*cred)->safeGetFloat("debt"); 
    }
    
    if (money < 0) money = 0; 
    hoi->resetLeaf("vicmoney", money);

    hoi->resetLeaf("vic_oil", (*vic)->safeGetString("oil"));
    hoi->resetLeaf("vic_rares", (*vic)->safeGetString("rares"));
    hoi->resetLeaf("vic_metal", (*vic)->safeGetString("metal"));
    hoi->resetLeaf("vic_energy", (*vic)->safeGetString("energy"));

    hoi->resetLeaf("vic_transports", (*vic)->safeGetFloat("vic_transports")*(100 + (*vic)->safeGetFloat("prestige")));
    hoi->resetLeaf("vic_escorts", (*vic)->safeGetFloat("vic_escorts")*(100 + (*vic)->safeGetFloat("prestige")));
    Logger::logStream(DebugStockpiles) << "  " << hoi->safeGetString("tag") << " : ("
				       << hoi->safeGetString("vic_transports") << ", "
				       << hoi->safeGetString("vic_escorts") << ") ("
				       << (*vic)->safeGetString("vic_transports") << ", "
				       << (*vic)->safeGetString("vic_escorts") << ")\n"; 
  }
  distribute(hoiCountries, "money", "vicmoney");
  distribute(hoiCountries, "oil", "vic_oil");
  distribute(hoiCountries, "rare_materials", "vic_rares");
  distribute(hoiCountries, "metal", "vic_metal");
  distribute(hoiCountries, "energy", "vic_energy");
  distribute(hoiCountries, "manpower", "vic_manpower", false, true);
  distribute(hoiCountries, "transports", "vic_transports", true);
  distribute(hoiCountries, "escorts", "vic_escorts", true);

  hoiCountries[0]->resetLeaf("mobManpower", configObject->safeGetFloat("mobManpower"));
  distribute(hoiCountries, "mobManpower", "totalPop");
}

void WorkerThread::mobEvents () {
  Object* mEvents = configObject->safeGetObject("mobEvents");
  if (!mEvents) return;

  int percent = mEvents->safeGetInt("losePercentage", 10); 
  
  Object* allEventsObject = new Object("mobEvents");
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    int mobManpower = (int) floor((*hoi)->safeGetFloat("mobManpower") + 0.5);
    if (10 > mobManpower) continue;

    string hoitag = (*hoi)->safeGetString("tag");
    Object* event = new Object("event");
    allEventsObject->setValue(event); 
    event->setLeaf("id", eventId++);
    event->setLeaf("random", "no");
    event->setLeaf("country", hoitag); 
    Object* trigger = new Object("trigger");
    event->setValue(trigger);
    Object* outerOR = new Object("OR");
    trigger->setValue(outerOR);
    Object* lost = new Object("lost_VP");
    outerOR->setValue(lost);
    lost->setLeaf("country", hoitag);
    lost->setLeaf("value", percent);
    lost = new Object("lost_national");
    outerOR->setValue(lost);
    lost->setLeaf("country", hoitag);
    lost->setLeaf("value", percent);
    lost = new Object("lost_IC");
    outerOR->setValue(lost);
    lost->setLeaf("country", hoitag);
    lost->setLeaf("value", percent);


    string eventName = "\"Call up the Reserve\"";
    string eventDesc = "\"The situation is desperate. We must mobilise all our reserves!\"";
    string actionName = "\"Very well, call out the reserves.\"";

    Object* kustom = mEvents->safeGetObject(hoitag); 
    if (kustom) {
      eventName = kustom->safeGetString("eventName", eventName);
      eventDesc = kustom->safeGetString("eventDesc", eventDesc);
      actionName = kustom->safeGetString("actionName", actionName); 
    }
    
    event->setLeaf("name", eventName);
    event->setLeaf("desc", eventDesc); 
    event->setLeaf("style", "0");
  
    Object* date = new Object("date");
    date->setLeaf("day", "1");
    date->setLeaf("month", "january"); 
    date->setLeaf("year", "1936");
    event->setValue(date);
    event->setLeaf("offset", "10"); 

    Object* action = new Object("action_a");
    event->setValue(action);
    
    action->setLeaf("name", actionName); 
    Object* command = new Object("command");
    action->setValue(command);
    command->setLeaf("type", "manpowerpool");
    command->setLeaf("value", mobManpower); 
    
  }
  
  ofstream writer;
  writer.open(".\\Output\\db\\events\\mobilisation_events.txt");  
  Parser::topLevel = allEventsObject; 
  writer << (*allEventsObject);
  writer.close();

}

void WorkerThread::calculateKustomPoints () {
  Object* voting = configObject->safeGetObject("votingAllowed");
  if (!voting) {
    Logger::logStream(Logger::Warning) << "Could not find votingAllowed object in config. Will default to universal voting.\n";
    voting = new Object("votingAllowed");
  }
  
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    Object* owner = findVicCountryByVicTag((*vic)->safeGetString("owner"));
    if (!owner) continue;

    ClassInfo* classes = vicCountryToClassInfoMap[owner];
    if (!classes) {
      classes = new ClassInfo();
      vicCountryToClassInfoMap[owner] = classes;      
    }

    VoterInfo* voters = vicCountryToVoterInfoMap[owner];
    if (!voters) {
      voters = new VoterInfo();
      vicCountryToVoterInfoMap[owner] = voters;
    }

    // Find cultures 
    string primary = remQuotes(owner->safeGetString("primary_culture"));
    vector<string> secondaries;
    Object* secs = owner->safeGetObject("culture");
    if (secs) {
      for (int i = 0; i < secs->numTokens(); ++i) {
	secondaries.push_back(remQuotes(secs->getToken(i))); 
      }
    }
    
    objvec leaves = (*vic)->getLeaves();
    for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
      double literacy = (*l)->safeGetFloat("literacy", -1);
      if (0 > literacy) continue;
      double size = (*l)->safeGetFloat("size", -1);
      if (0 > size) continue;
      string poptype = (*l)->getKey();
      classes->addTotal(poptype, size);
      classes->addWeightedTotal(poptype, size*literacy);
      if ((*l)->safeGetString("acceptedCulture", "no") == "yes") {
	classes->addCulture(poptype, size);
	classes->addWeightedCulture(poptype, size*literacy);
      }
      voters->addPop((*l), voting->safeGetObject(owner->safeGetString("vote_franschise", "none_voting"))); // Vicky mis-spells this. 
    }
  }
}

void WorkerThread::desperationMoveCapitals () {
  // If there is an enemy army in your capital, move to somewhere with a friendly army.

  objvec listOfCapitals;
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* cap = findHoiProvinceFromHoiId((*hoi)->safeGetString("capital"));
    if (cap) listOfCapitals.push_back(cap); 
  }

  objvec armiesInEnemyCapitals; 
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    objvec armies = (*hoi)->getValue("landunit");
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      Object* loc = findHoiProvinceFromHoiId((*army)->safeGetString("location"));
      if (find(listOfCapitals.begin(), listOfCapitals.end(), loc) == listOfCapitals.end()) continue;
      Object* hoiOwner = findHoiCountryByHoiTag(loc->safeGetString("owner"));
      if (hoiOwner == (*hoi)) continue;
      if (!(hoiCountriesRelations[hoiOwner][*hoi] & 1)) continue;


      objvec candidates;
      for (objiter cand = hoiProvinces.begin(); cand != hoiProvinces.end(); ++cand) {
	if ((*cand)->safeGetString("owner") != hoiOwner->safeGetString("tag")) continue;
	if ((*cand) == loc) continue; 
	candidates.push_back(*cand); 
      }

      Object* bestCandidate = 0;
      int unitSize = 0; 
      objvec loyalArmies = hoiOwner->getValue("landunit");
      for (objiter army = loyalArmies.begin(); army != loyalArmies.end(); ++army) {
	Object* loyloc = findHoiProvinceFromHoiId((*army)->safeGetString("location"));
	if (find(candidates.begin(), candidates.end(), loyloc) == candidates.end()) continue;
	int size = (*army)->getValue("division").size(); 
	if (!bestCandidate) {
	  bestCandidate = loyloc;
	  unitSize = size; 
	}
	else {
	  if (size < unitSize) continue;
	  unitSize = size;
	  bestCandidate = loyloc; 
	}
      }

      if (!bestCandidate) {
	Logger::logStream(Logger::Game) << "Could not find improved capital for " << hoiOwner->safeGetString("tag") << "\n";
	continue; 
      }
      Logger::logStream(Logger::Game) << "Moving capital of " << hoiOwner->safeGetString("tag") << " to " << bestCandidate->safeGetString("id")
				      << " due to enemy units.\n"; 
      
      int vicPoints = loc->safeGetInt("points");
      if (vicPoints > 0) loc->resetLeaf("points", vicPoints - 1);
      hoiOwner->resetLeaf("capital", bestCandidate->safeGetString("id"));
      bestCandidate->resetLeaf("points", 1 + bestCandidate->safeGetInt("points")); 
    }
  }
}

void WorkerThread::diplomacy () {
  Object* vicDip = vicGame->safeGetObject("diplomacy");
  if (!vicDip) vicDip = new Object("diplomacy"); 

  Object* dummy = new Object("dummy"); 

  map<Object*, Object*> overlords;
  map<Object*, map<Object*, bool> > alliances; 
  objvec vicvas = vicDip->getValue("vassal");
  for (objiter vv = vicvas.begin(); vv != vicvas.end(); ++vv) {
    Object* boss = findVicCountryByVicTag((*vv)->safeGetString("first"));
    Object* puppet = findVicCountryByVicTag((*vv)->safeGetString("second"));
    overlords[puppet] = boss; 
  }

  objvec vicall = vicDip->getValue("vassal");
  for (objiter vv = vicall.begin(); vv != vicall.end(); ++vv) {
    Object* boss = findVicCountryByVicTag((*vv)->safeGetString("first"));
    Object* puppet = findVicCountryByVicTag((*vv)->safeGetString("second"));
    alliances[puppet][boss] = true;
    alliances[boss][puppet] = true;
  }

  
  double bbFactor = configObject->safeGetFloat("badboyFactor");
  for (objiter hoi1 = hoiCountries.begin(); hoi1 != hoiCountries.end(); ++hoi1) {
    Object* vic1 = findVicCountryByHoiCountry(*hoi1);
    if (!vic1) continue;
    (*hoi1)->resetLeaf("belligerence", bbFactor*vic1->safeGetFloat("badboy")); 

    Object* hoiDip = new Object("diplomacy");
    (*hoi1)->setValue(hoiDip); 
    
    for (objiter hoi2 = hoiCountries.begin(); hoi2 != hoiCountries.end(); ++hoi2) {
      if ((*hoi1) == (*hoi2)) continue; 
      
      Object* vic2 = findVicCountryByHoiCountry(*hoi2);
      if (!vic2) continue;

      if (vic1 == vic2) continue; 

      Object* vicRelation = vic1->safeGetObject(vic2->getKey());
      if (!vicRelation) vicRelation = dummy;

      double value = vicRelation->safeGetInt("value");
      Object* hoiRelation = new Object("relation");
      hoiRelation->setLeaf("tag", (*hoi2)->safeGetString("tag")); 
      hoiRelation->setLeaf("value", value);
      if (vicRelation->safeGetString("military_access", "no") == "yes") {
	hoiRelation->setLeaf("access", "yes");
	hoiCountriesRelations[*hoi1][*hoi2] |= 2; 	
      }
      hoiDip->setValue(hoiRelation);
      
      if (overlords[vic1] == vic2) (*hoi1)->resetLeaf("puppet", addQuotes((*hoi2)->safeGetString("tag"))); 
    }
  }
}

void WorkerThread::dissent () {
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* hoi = findHoiCountryByVicCountry(*vic);
    if (!hoi) continue;
    double diss = vicCountryToVoterInfoMap[*vic] ? vicCountryToVoterInfoMap[*vic]->getDissent() : 0; 
    hoi->resetLeaf("dissent", diss); 
  }
}

void WorkerThread::fixHeader () {
  Object* header = hoiGame->safeGetObject("header");
  assert(header);

  Object* selectable = header->safeGetObject("selectable");
  if (!selectable) {
    selectable = new Object("selectable");
    selectable->setObjList(true);
    header->setValue(selectable);
  }

  selectable->clear();
  map<Object*, bool> gotAlready;
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* hoi = findHoiCountryByVicCountry(*vic);
    if (!hoi) continue; 
    if (gotAlready[hoi]) continue;
    if (hoi->safeGetString("tag") == "REB") continue; 
    gotAlready[hoi] = true;
    selectable->addToList(hoi->safeGetString("tag"));
  }

  objvec toRemove;
  objvec headerLeaves = header->getLeaves();
  for (objiter h = headerLeaves.begin(); h != headerLeaves.end(); ++h) {
    if ((*h)->safeGetString("countrytactics", "blah") == "blah") continue;
    toRemove.push_back(*h);
  }
  for (objiter t = toRemove.begin(); t != toRemove.end(); ++t) {
    header->removeObject(*t);
  }

  int numSelectables = configObject->safeGetInt("numSelectable", 12);
  ObjectDescendingSorter sorter("numOwned");
  sort(hoiCountries.begin(), hoiCountries.end(), sorter);
  for (int i = 0; i < numSelectables; ++i) {
    string tag = hoiCountries[i]->safeGetString("tag");
    Object* headerInfo = new Object(tag);
    header->setValue(headerInfo);
    headerInfo->setLeaf("songs", "\"\"");
    sprintf(stringbuffer, "\"%s_DESC\"", tag.c_str());
    headerInfo->setLeaf("desc", stringbuffer);
    headerInfo->setLeaf("countrytactics", stringbuffer); 
    sprintf(stringbuffer, "\"scenarios\\data\\propaganda_%s.bmp\"", tag.c_str());
    headerInfo->setLeaf("picture", stringbuffer);
  }
}

void WorkerThread::fixGlobals () {
  Object* globals = hoiGame->safeGetObject("globaldata");
  assert(globals);
  globals->unsetValue("alliance");
  globals->unsetValue("war");
  globals->unsetValue("treaty");
  globals->unsetValue("weather");
  globals->unsetValue("disbandlosses");
  globals->unsetValue("attritionlosses");
  globals->unsetValue("combatlosses");
  globals->unsetValue("inflictedlosses");     

  Object* axis = globals->safeGetObject("axis");
  if (!axis) {
    axis = new Object("axis");
    globals->setValue(axis);
    Object* id = new Object("id");
    axis->setValue(id); 
    id->setLeaf("type", "15000");
    id->setLeaf("id", 2);
    axis->setLeaf("defensive", "no"); 
  }
  Object* comintern = globals->safeGetObject("comintern");
  if (!comintern) {
    comintern = new Object("comintern");
    globals->setValue(comintern);
    Object* id = new Object("id");
    comintern->setValue(id); 
    id->setLeaf("type", "15000");
    id->setLeaf("id", 3);
    comintern->setLeaf("defensive", "no"); 
  }
  Object* allies = globals->safeGetObject("allies");
  if (!allies) {
    allies = new Object("allies");
    globals->setValue(allies);
    Object* id = new Object("id");
    allies->setValue(id); 
    id->setLeaf("type", "15000");
    id->setLeaf("id", 1);
    allies->setLeaf("defensive", "no"); 
  }

  ObjectDescendingSorter milsorter("vicmilsize");
  sort(hoiCountries.begin(), hoiCountries.end(), milsorter);

  Object* fascists = hoiCountries[0];
  int rulingParty = fascists->safeGetInt("ruling_party");
  string currIdeology = vicParties[rulingParty]->safeGetString("ideology"); 
  for (int i = 1; i < 8; ++i) {
    // Search for Axis: Fascist, reactionary, conservative, anything.
    if (currIdeology == "fascist") break;
    
    rulingParty = hoiCountries[i]->safeGetInt("ruling_party");
    string id = vicParties[rulingParty]->safeGetString("ideology");
    if (id == "fascist") {
      fascists = hoiCountries[i];
      break; 
    }
    
    if ((id == "reactionary") && (currIdeology != "reactionary")) {
      fascists = hoiCountries[i];
      currIdeology = id; 
    }
    if ((id == "conservative") && (currIdeology != "reactionary") && (currIdeology != "conservative")) {
      fascists = hoiCountries[i];
      currIdeology = id; 
    }
  }

  Object* commies = hoiCountries[0];
  if (fascists == commies) commies = hoiCountries[1];
  rulingParty = commies->safeGetInt("ruling_party");
  currIdeology = vicParties[rulingParty]->safeGetString("ideology"); 
  for (int i = 1; i < 8; ++i) {
    // Search for Comintern: Communist, socialist, anarcho_liberal, fascist, anything
    if (currIdeology == "communist") break;
    if (fascists == hoiCountries[i]) continue; 
    
    rulingParty = hoiCountries[i]->safeGetInt("ruling_party");
    string id = vicParties[rulingParty]->safeGetString("ideology");
    if (id == "communist") {
      commies = hoiCountries[i];
      break; 
    }
    
    if ((id == "socialist") && (currIdeology != "socialist")) {
      commies = hoiCountries[i];
      currIdeology = id; 
    }
    if ((id == "anarcho_liberal") && (currIdeology != "socialist") && (currIdeology != "anarcho_liberal")) {
      commies = hoiCountries[i];
      currIdeology = id; 
    }
    if ((id == "fascist") && (currIdeology != "fascist") & (currIdeology != "socialist") && (currIdeology != "anarcho_liberal")) {
      commies = hoiCountries[i];
      currIdeology = id; 
    }    
  }

  Object* democrats = hoiCountries[0];
  if ((fascists == democrats) || (commies == democrats)) democrats = hoiCountries[1];
  if ((fascists == democrats) || (commies == democrats)) democrats = hoiCountries[2];  
  rulingParty = hoiCountries[0]->safeGetInt("ruling_party");
  currIdeology = vicParties[rulingParty]->safeGetString("ideology"); 
  for (int i = 1; i < 8; ++i) {
    // Search for Democrats: Liberal or conservative with equal priority, then whatever
    if (currIdeology == "communist") break;
    if (fascists == hoiCountries[i]) continue;
    if (commies == hoiCountries[i]) continue;     
    
    rulingParty = hoiCountries[i]->safeGetInt("ruling_party");
    string id = vicParties[rulingParty]->safeGetString("ideology");
    if (id == "liberal") {
      democrats = hoiCountries[i];
      break; 
    }
    if (id == "conservative") {
      democrats = hoiCountries[i];
      break; 
    }
  }

  map<Object*, Object*> countryToAllianceMap; 
  Object* participant = axis->safeGetObject("participant");
  if (!participant) {
    participant = new Object("participant");
    participant->setObjList(true); 
    axis->setValue(participant);
  }
  participant->clear();
  participant->addToList(fascists->safeGetString("tag"));
  countryToAllianceMap[fascists] = axis; 

  participant = comintern->safeGetObject("participant");
  if (!participant) {
    participant = new Object("participant");
    participant->setObjList(true); 
    comintern->setValue(participant);
  }
  participant->clear();
  participant->addToList(commies->safeGetString("tag"));
  countryToAllianceMap[commies] = comintern;

  participant = allies->safeGetObject("participant");
  if (!participant) {
    participant = new Object("participant");
    participant->setObjList(true); 
    allies->setValue(participant);
  }
  participant->clear();
  participant->addToList(democrats->safeGetString("tag"));
  countryToAllianceMap[democrats] = allies; 

  Object* dummyAlliance = new Object("dummy"); 
  
  int allyCounter = 10; 
  Object* dip = vicGame->safeGetObject("diplomacy");
  if (!dip) dip = new Object("diplomacy");
  objvec vicAlliances = dip->getValue("alliance");
  for (objiter a = vicAlliances.begin(); a != vicAlliances.end(); ++a) {
    Object* vic1 = findVicCountryByVicTag(remQuotes((*a)->safeGetString("first")));
    if (!vic1) continue;
    Object* hoi1 = findHoiCountryByVicCountry(vic1);
    if (!hoi1) continue;

    Object* vic2 = findVicCountryByVicTag(remQuotes((*a)->safeGetString("second")));
    if (!vic2) continue;
    Object* hoi2 = findHoiCountryByVicCountry(vic2);
    if (!hoi2) continue;

    if ((countryToAllianceMap[hoi1]) && (countryToAllianceMap[hoi2])) continue;
    if (countryToAllianceMap[hoi1] == dummyAlliance) continue;
    if (countryToAllianceMap[hoi2] == dummyAlliance) continue;
    
    if ((countryToAllianceMap[hoi1]) && (!countryToAllianceMap[hoi2])) {
      countryToAllianceMap[hoi1]->safeGetObject("participant")->addToList(hoi2->safeGetString("tag"));
      countryToAllianceMap[hoi2] = countryToAllianceMap[hoi1];
      continue;
    }
    if ((countryToAllianceMap[hoi2]) && (!countryToAllianceMap[hoi1])) {
      countryToAllianceMap[hoi2]->safeGetObject("participant")->addToList(hoi1->safeGetString("tag"));
      countryToAllianceMap[hoi1] = countryToAllianceMap[hoi2];
      continue;
    }

    Object* alliance = new Object("alliance");
    globals->setValue(alliance);
    Object* allid = new Object("id");
    alliance->setValue(allid);
    allid->setLeaf("type", "15000");
    allid->setLeaf("id", allyCounter++);

    alliance->setLeaf("defensive", "no");
    Object* part = new Object("participant");
    alliance->setValue(part);
    part->setObjList(true);
    part->addToList(hoi1->safeGetString("tag"));
    part->addToList(hoi2->safeGetString("tag"));

    countryToAllianceMap[hoi1] = dummyAlliance;
    countryToAllianceMap[hoi2] = dummyAlliance;
  }

  objvec vicWars = vicGame->getValue("active_war");
  int warCounter = 1; 
  for (objiter vicWar = vicWars.begin(); vicWar != vicWars.end(); ++vicWar) {
    Object* hoiWar = new Object("war");
    globals->setValue(hoiWar);

    Object* warid = new Object("id");
    hoiWar->setValue(warid);
    warid->setLeaf("type", "9430");
    warid->setLeaf("id", warCounter++);

    objvec leaves = (*vicWar)->getLeaves();
    objiter earliest = leaves.end(); 
    int firstDate = 10000000;
    for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
      string tag = (*l)->safeGetString("add_attacker", "BLAH");
      if (tag == "BLAH") continue;
      int curr = days((*l)->getKey());
      if (curr > firstDate) continue;
      firstDate = curr;
      earliest = l; 
    }

    string year("1936");
    string month("january");
    string day("0"); 
    if (earliest != leaves.end()) {
      string start = (*earliest)->getKey();
      year  = getField(start, 0, '.');
      month = getField(start, 1, '.');
      day   = getField(start, 2, '.');
      month = convertMonth(month); 
    }
    
    Object* date = new Object("date");
    hoiWar->setValue(date);
    date->setLeaf("year", year); 
    date->setLeaf("month", month); 
    date->setLeaf("day", day); 
    date->setLeaf("hour", "0");

    Object* att = new Object("attackers");
    hoiWar->setValue(att);
    warid = new Object("id");
    att->setValue(warid);
    warid->setLeaf("type", "9430");
    warid->setLeaf("id", warCounter++);
    att->setLeaf("defensive", "no");
    Object* part = new Object("participant");
    part->setObjList(true);
    att->setValue(part);
    objvec attackers = (*vicWar)->getValue("attacker");
    for (objiter a = attackers.begin(); a != attackers.end(); ++a) {
      Object* vic = findVicCountryByVicTag(remQuotes((*a)->getLeaf()));
      if (!vic) continue;
      Object* hoi = findHoiCountryByVicCountry(vic);
      if (!hoi) continue;
      part->addToList(hoi->safeGetString("tag"));
    }

    Object* def = new Object("defenders");
    hoiWar->setValue(def);
    warid = new Object("id");
    def->setValue(warid);
    warid->setLeaf("type", "9430");
    warid->setLeaf("id", warCounter++);
    def->setLeaf("defensive", "no");
    part = new Object("participant");
    part->setObjList(true);
    def->setValue(part);
    objvec defenders = (*vicWar)->getValue("defender");
    for (objiter d = defenders.begin(); d != defenders.end(); ++d) {
      Object* vic = findVicCountryByVicTag(remQuotes((*d)->getLeaf()));
      if (!vic) continue;
      Object* hoi = findHoiCountryByVicCountry(vic);
      if (!hoi) continue;
      part->addToList(hoi->safeGetString("tag"));

      for (objiter a = attackers.begin(); a != attackers.end(); ++a) {
	Object* vicAtt = findVicCountryByVicTag(remQuotes((*a)->getLeaf()));
	if (!vicAtt) continue;
	Object* hoiAtt = findHoiCountryByVicCountry(vicAtt);
	if (!hoiAtt) continue;
	hoiCountriesRelations[hoiAtt][hoi] |= 1;
	hoiCountriesRelations[hoi][hoiAtt] |= 1; 
      }
    }

      
  }
}

int WorkerThread::issueToNumber (string issue) const {
  static Object* issues = 0;
  if (!issues) {
    issues = configObject->safeGetObject("issue_numbers");
    if (!issues) {
      Logger::logStream(Logger::Error) << "Error: No issue numbers found, returning 0 for " << issue << "\n";
      return 0; 
    }
  }

  return issues->safeGetInt(issue); 
}

void WorkerThread::findBestIdea (objvec& ideas, Object* vicCountry, vector<string>& qualia) {
  Object* hoi = findHoiCountryByVicCountry(vicCountry);
  if (!hoi) return; 

  double bestDist = 10; 
  Object* bestIdea = ideas[0];
  
  for (objiter idea = ideas.begin(); idea != ideas.end(); ++idea) {
    bool allowed = false;
    objvec allowedGovs = (*idea)->getValue("category");
    for (objiter g = allowedGovs.begin(); g != allowedGovs.end(); ++g) {
      if (hoi->safeGetString("govType") != (*g)->getLeaf()) continue;
      allowed = true;
      break;
    }
    if (!allowed) continue;

    double distance = 0; 
    for (vector<string>::iterator q = qualia.begin(); q != qualia.end(); ++q) {
      double curr = (*idea)->safeGetFloat(*q, -1);
      if (0 > curr) continue;
      curr -= vicCountry->safeGetFloat(*q);
      distance = sqrt(distance*distance + curr*curr);
    }
    Logger::logStream(DebugIdeas) << "    " << (*idea)->safeGetString("personality_string") << ": " << distance << "\n"; 
    if (distance > bestDist) continue;
    bestDist = distance;
    bestIdea = (*idea); 
  }

  hoi->resetLeaf(bestIdea->getKey(), bestIdea->safeGetString("personality_string"));
}

void WorkerThread::ideas () {
  // See documentation. 
  vector<string> qualia;
  qualia.push_back("offensiveness");
  qualia.push_back("activeness");
  qualia.push_back("nurtureness");
  qualia.push_back("statism");
  qualia.push_back("essentialism");
  qualia.push_back("outerness");

  Logger::logStream(DebugIdeas) << "National ideas:\n"; 
  Object* ideaObject = loadTextFile(targetVersion + "ideas.txt");
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Object* hoi = findHoiCountryByVicCountry(*vic); 
    if (!hoi) continue; 

    
    Logger::logStream(DebugIdeas) << "  " << (*vic)->getKey() << "(" << hoi->safeGetString("govType") << ") :\n";
    for (vector<string>::iterator q = qualia.begin(); q != qualia.end(); ++q) {
      Logger::logStream(DebugIdeas) << "    " << (*q) << " : " << (*vic)->safeGetFloat(*q) << "\n"; 
    }
    
    objvec ideas = ideaObject->getValue("nationalidentity");
    findBestIdea(ideas, (*vic), qualia);
    ideas = ideaObject->getValue("socialpolicy");
    findBestIdea(ideas, (*vic), qualia);
    ideas = ideaObject->getValue("nationalculture");
    findBestIdea(ideas, (*vic), qualia);

    Logger::logStream(DebugIdeas) << "  Selected "
				  << hoi->safeGetString("nationalidentity") << ", " 
				  << hoi->safeGetString("socialpolicy") << ", " 
				  << hoi->safeGetString("nationalculture") << "\n"; 
  }
}


void normaliseQuantity (objvec& vics, string keyword) {
  double maxQ = 0; 
  for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
    double curr = (*vic)->safeGetFloat(keyword);
    if (curr < maxQ) continue;
    maxQ = curr;
  }
  for (objiter vic = vics.begin(); vic != vics.end(); ++vic) {
    double curr = (*vic)->safeGetFloat(keyword);
    curr /= maxQ;
    if (curr < 0) curr = 0; 
    (*vic)->resetLeaf(keyword, curr);
  }  
}

void WorkerThread::calculateCountryQualities () {
  for (objiter vp = vicProvinces.begin(); vp != vicProvinces.end(); ++vp) {
    Object* owner = findVicCountryByVicTag((*vp)->safeGetString("owner"));
    if (!owner) continue;
    owner->resetLeaf("ownedProvinces", 1 + owner->safeGetInt("ownedProvinces"));
    objvec cores = (*vp)->getValue("core");
    bool ownerHasCore = false; 
    for (objiter core = cores.begin(); core != cores.end(); ++core) {
      Object* coreOwner = findVicCountryByVicTag((*core)->getLeaf());
      if (!coreOwner) continue;
      if (coreOwner == owner) {
	owner->resetLeaf("ownedCores", 1 + owner->safeGetInt("ownedCores"));
	ownerHasCore= true; 
      }
      else coreOwner->resetLeaf("unOwnedCores", 1 + coreOwner->safeGetInt("unOwnedCores"));
    }
    if (!ownerHasCore) owner->resetLeaf("ownedNonCores", 1 + owner->safeGetInt("ownedNonCores")); 
  }

  objvec wars = vicGame->getValue("previous_war");
  for (objiter war = wars.begin(); war != wars.end(); ++war) {
    Object* history = (*war)->safeGetObject("history");
    int losses = 0;
    objvec battles = history->getValue("battle");
    for (objiter b = battles.begin(); b != battles.end(); ++b) {
      Object* part = (*b)->safeGetObject("attacker");
      if (part) losses += part->safeGetInt("losses");
      part = (*b)->safeGetObject("defender");
      if (part) losses += part->safeGetInt("losses");
    }
    Object* attacker = findVicCountryByVicTag((*war)->safeGetString("original_attacker"));
    if (attacker) attacker->resetLeaf("aggressive_war", losses + attacker->safeGetInt("aggressive_war"));
    Object* defender = findVicCountryByVicTag((*war)->safeGetString("original_defender"));
    if (defender) defender->resetLeaf("defensive_war", losses + defender->safeGetInt("defensive_war"));    
  }

  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    VoterInfo* voters = vicCountryToVoterInfoMap[*vic];
    double diversity = 0;
    if (voters) diversity = voters->calculateDiversity();
    (*vic)->resetLeaf("diversity", diversity);
    (*vic)->resetLeaf("norm_revenge", (*vic)->safeGetFloat("revanchism"));

    VoterInfo* votes = vicCountryToVoterInfoMap[*vic];
    if (votes) {
      double warsupport  = votes->getIssuePercentage(issueToNumber("jingoism"));
      warsupport        += 0.5*votes->getIssuePercentage(issueToNumber("pro_military"));
      (*vic)->resetLeaf("war_support", warsupport); 
      
      double citizenship = votes->getIssuePercentage(issueToNumber("full_citizenship"));
      (*vic)->resetLeaf("citizenship_support", citizenship); 
      
      double plannedecon = votes->getIssuePercentage(issueToNumber("planned_economy"));
      (*vic)->resetLeaf("planned_econ_support", plannedecon);
      
      double residency   = votes->getIssuePercentage(issueToNumber("residency"));     
      residency         += votes->getIssuePercentage(issueToNumber("moralism"));
      (*vic)->resetLeaf("residency_support", residency);

      residency          = votes->getIssuePercentage(issueToNumber("limited_citizenship"));     
      residency         += votes->getIssuePercentage(issueToNumber("full_citizenship"));
      (*vic)->resetLeaf("citizenship_support", residency);       
    }

    string dateOfLostWar = remQuotes((*vic)->safeGetString("last_lost_war", "\"1.1.1\""));
    (*vic)->resetLeaf("lost_war_time", configObject->safeGetInt("current_date_days") - days(dateOfLostWar));

    ClassInfo* classes = vicCountryToClassInfoMap[*vic];
    if (classes) {
      (*vic)->resetLeaf("norm_bureaucrats", classes->getPercentage("bureaucrats"));
      (*vic)->resetLeaf("norm_aristos", classes->getPercentage("aristocrats") / (0.001 + classes->getPercentage("capitalists")));
      (*vic)->resetLeaf("norm_officers", classes->getPercentage("officers") / (0.001 + classes->getPercentage("clerics"))); 
    }

    Object* upperhouse = (*vic)->safeGetObject("upper_house");
    if (upperhouse) {
      (*vic)->resetLeaf("norm_fascism", upperhouse->safeGetFloat("fascist") + upperhouse->safeGetFloat("communist"));
      (*vic)->resetLeaf("norm_reaction", upperhouse->safeGetFloat("fascist") + upperhouse->safeGetFloat("reactionary") + upperhouse->safeGetFloat("conservative"));
      (*vic)->resetLeaf("norm_communism", upperhouse->safeGetFloat("communist") + upperhouse->safeGetFloat("socialist") + upperhouse->safeGetFloat("anarcho_liberal")); 
    }

    Object* foreign = (*vic)->safeGetObject("foreign_investment");
    if (foreign) {
      double invest = 0;
      for (int i = 0; i < foreign->numTokens(); ++i) {
	string curr = foreign->getToken(i);
	invest += atof(curr.c_str()); 
      }
      (*vic)->resetLeaf("norm_investments", invest); 
    }
  }

  
  normaliseQuantity(vicCountries, "aggressive_war");
  normaliseQuantity(vicCountries, "defensive_war");
  normaliseQuantity(vicCountries, "unOwnedCores");
  normaliseQuantity(vicCountries, "diversity");
  normaliseQuantity(vicCountries, "norm_revenge");
  normaliseQuantity(vicCountries, "ownedNonCores");
  normaliseQuantity(vicCountries, "war_support");
  normaliseQuantity(vicCountries, "citizenship_support");
  normaliseQuantity(vicCountries, "planned_econ_support");
  normaliseQuantity(vicCountries, "residency_support");
  normaliseQuantity(vicCountries, "lost_war_time");
  normaliseQuantity(vicCountries, "norm_bureaucrats");
  normaliseQuantity(vicCountries, "norm_fascism");
  normaliseQuantity(vicCountries, "norm_communism"); 
  normaliseQuantity(vicCountries, "norm_officers");
  normaliseQuantity(vicCountries, "norm_aristos");
  normaliseQuantity(vicCountries, "norm_reaction");
  normaliseQuantity(vicCountries, "norm_investments"); 

  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    (*vic)->resetLeaf("offensiveness", (*vic)->safeGetFloat("aggressive_war") + (*vic)->safeGetFloat("unOwnedCores") + (*vic)->safeGetFloat("norm_revenge"));
    (*vic)->resetLeaf("activeness", (*vic)->safeGetFloat("ownedNonCores") + (*vic)->safeGetFloat("lost_war_time") + (*vic)->safeGetFloat("war_support") - (*vic)->safeGetFloat("defensive_war")); 
    (*vic)->resetLeaf("nurtureness", (*vic)->safeGetFloat("diversity") + (*vic)->safeGetFloat("citizenship_support"));
    (*vic)->resetLeaf("statism", (*vic)->safeGetFloat("planned_econ_support") + (*vic)->safeGetFloat("norm_fascism") + (*vic)->safeGetFloat("norm_bureaucrats")); 
    (*vic)->resetLeaf("essentialism", (*vic)->safeGetFloat("norm_reaction") + (*vic)->safeGetFloat("residency_support") + (*vic)->safeGetFloat("norm_aristos"));
    (*vic)->resetLeaf("outerness", (*vic)->safeGetFloat("norm_investments") + (*vic)->safeGetFloat("norm_officers"));
  }
  
  normaliseQuantity(vicCountries, "offensiveness");
  normaliseQuantity(vicCountries, "activeness");
  normaliseQuantity(vicCountries, "nurtureness");
  normaliseQuantity(vicCountries, "statism");
  normaliseQuantity(vicCountries, "essentialism");
  normaliseQuantity(vicCountries, "outerness");

  Logger::logStream(DebugIdeas) << "National qualities:\n"; 
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    Logger::logStream(DebugIdeas) << "  " << (*vic)->getKey() << ":\n"
				  << "    Offensiveness: "
				  << (*vic)->safeGetFloat("offensiveness") << " "
				  << (*vic)->safeGetFloat("aggressive_war") << " "
				  << (*vic)->safeGetFloat("unOwnedCores") << " "
				  << (*vic)->safeGetFloat("norm_revenge") << "\n    Activeness: "
      				  << (*vic)->safeGetFloat("activeness") << " "
      				  << (*vic)->safeGetFloat("ownedNonCores") << " "
      				  << (*vic)->safeGetFloat("lost_war_time") << " "
      				  << (*vic)->safeGetFloat("war_support") << " "
      				  << (*vic)->safeGetFloat("defensive_war") << "\n    Nurtureness: "
      				  << (*vic)->safeGetFloat("nurtureness") << " "
      				  << (*vic)->safeGetFloat("diversity") << " "
      				  << (*vic)->safeGetFloat("citizenship_support") << "\n    Statism: "
				  << (*vic)->safeGetFloat("statism") << " "
				  << (*vic)->safeGetFloat("planned_econ_support") << " "
				  << (*vic)->safeGetFloat("norm_fascism") << " "
				  << (*vic)->safeGetFloat("norm_bureaucrats") << "\n    Essentialism: "
				  << (*vic)->safeGetFloat("essentialism") << " "
				  << (*vic)->safeGetFloat("norm_reaction") << " "
				  << (*vic)->safeGetFloat("residency_support") << " "
				  << (*vic)->safeGetFloat("norm_aristos") << "\n    Outerness: "
				  << (*vic)->safeGetFloat("outerness") << " "
				  << (*vic)->safeGetFloat("norm_investments") << " "
				  << (*vic)->safeGetFloat("norm_officers") << "\n";
  }
}

void WorkerThread::prepareCountries () {
  generals.resize(4);
  admirals.resize(4);
  commders.resize(4);

  vicParties.resize(5000); 
  int partyCounter = 1; 
  Object* partyFileList = loadTextFile(sourceVersion + "countries.txt");
  objvec pfiles = partyFileList->getLeaves();
  for (objiter file = pfiles.begin(); file != pfiles.end(); ++file) {
    string vtag = (*file)->getKey();
    string fname = remQuotes((*file)->getLeaf());
    Object* currParties = loadTextFile(sourceVersion + fname);
    objvec parties = currParties->getValue("party"); 
    for (objiter p = parties.begin(); p != parties.end(); ++p) {
      (*p)->setLeaf("partyNumber", partyCounter);
      if (partyCounter >= (int) vicParties.size()) vicParties.resize(2*partyCounter);
      vicParties[partyCounter] = (*p); 
      partyCounter++; 
    }
  }
  
  Object* globals = hoiGame->safeGetObject("globaldata");
  globals->unsetValue("treaty");
  globals->unsetValue("alliance");

  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    (*hoi)->unsetValue("convoy");
    (*hoi)->unsetValue("puppet");
    (*hoi)->unsetValue("control");
    (*hoi)->unsetValue("diplomacy");
    (*hoi)->unsetValue("province_development");

    objvec landunits = (*hoi)->getValue("landunit");
    for (objiter l = landunits.begin(); l != landunits.end(); ++l) {
      objvec divs = (*l)->getValue("division");
      for (objiter div = divs.begin(); div != divs.end(); ++div) {
	string unittype = (*div)->safeGetString("type");
	landdivs[unittype]++;
	string extra = (*div)->safeGetString("extra", "none");
	if (extra != "none") {
	  landbrigs[extra]++;
	  brigadeAcceptable[unittype][extra] = true;
	}
	double maxOil = (*div)->safeGetFloat("max_oil_stock");
	double maxSup = (*div)->safeGetFloat("max_supply_stock");
	if (unitTypeToMaxSuppliesMap[unittype][extra].first < maxOil) unitTypeToMaxSuppliesMap[unittype][extra].first = maxOil;
	if (unitTypeToMaxSuppliesMap[unittype][extra].second < maxSup) unitTypeToMaxSuppliesMap[unittype][extra].second = maxSup;
      }
    }

    objvec navalunits = (*hoi)->getValue("navalunit");
    for (objiter l = navalunits.begin(); l != navalunits.end(); ++l) {
      objvec divs = (*l)->getValue("division");
      for (objiter div = divs.begin(); div != divs.end(); ++div) {
	string unittype = (*div)->safeGetString("type");
	navaldivs[unittype]++;
	for (int i = 1; i < 5; ++i) {
	  sprintf(stringbuffer, "extra%i", i);
	  string extra = (*div)->safeGetString(stringbuffer, "none");
	  if (extra != "none") {
	    navalbrigs[extra]++;
	    brigadeAcceptable[unittype][extra] = true;
	  }
	  double maxOil = (*div)->safeGetFloat("max_oil_stock");
	  double maxSup = (*div)->safeGetFloat("max_supply_stock");	  
	  if (unitTypeToMaxSuppliesMap[unittype][extra].first < maxOil) unitTypeToMaxSuppliesMap[unittype][extra].first = maxOil;
	  if (unitTypeToMaxSuppliesMap[unittype][extra].second < maxSup) unitTypeToMaxSuppliesMap[unittype][extra].second = maxSup;
	}
      }
    }

    objvec airunits = (*hoi)->getValue("airunit");
    for (objiter l = airunits.begin(); l != airunits.end(); ++l) {
      objvec divs = (*l)->getValue("division");
      for (objiter div = divs.begin(); div != divs.end(); ++div) {
	string unittype = (*div)->safeGetString("type");
	airdivs[unittype]++;
	string extra = (*div)->safeGetString("extra", "none");
	if (extra != "none") airbrigs[extra]++;
	double maxOil = (*div)->safeGetFloat("max_oil_stock");
	double maxSup = (*div)->safeGetFloat("max_supply_stock");	
	if (unitTypeToMaxSuppliesMap[unittype][extra].first < maxOil) unitTypeToMaxSuppliesMap[unittype][extra].first = maxOil;
	if (unitTypeToMaxSuppliesMap[unittype][extra].second < maxSup) unitTypeToMaxSuppliesMap[unittype][extra].second = maxSup;
      }
    }
    
    (*hoi)->unsetValue("landunit");
    (*hoi)->unsetValue("navalunit");
    (*hoi)->unsetValue("airunit");

    
    objvec curr_ministers = (*hoi)->getValue("minister");
    for (objiter m = curr_ministers.begin(); m != curr_ministers.end(); ++m) {
      Object* id = (*m)->safeGetObject("id");
      if (!id) continue;
      int minister_id = id->safeGetInt("id", -1);
      if (-1 == minister_id) continue;
      if (minister_id > maxMinisterId) maxMinisterId = minister_id;
      string position = (*m)->safeGetString("position", "useless");
      ministers[(*m)->safeGetString("category", "undecided")][position].push_back(*m);
      personalities[position][(*m)->safeGetString("personality", "blandness")]++;
      personalities[position]["total"]++; 
    }
    (*hoi)->unsetValue("minister"); 

    Object* currGenerals = new Object("generals");
    Object* currAdmirals = new Object("admirals");
    Object* currCommders = new Object("commders"); 

    string hoitag = (*hoi)->safeGetString("tag");
    Object* officerNames = new Object("officerNames");
    (*hoi)->setValue(officerNames);
    objvec curr_officers = (*hoi)->getValue("leader");
    Logger::logStream(DebugLeaders) << "Found " << (int) curr_officers.size() << " leaders for " << hoitag << "\n"; 
    for (objiter m = curr_officers.begin(); m != curr_officers.end(); ++m) {
      Object* id = (*m)->safeGetObject("id");
      if (!id) continue;
      int officer_id = id->safeGetInt("id", -1);
      if (-1 == officer_id) continue;
      (*m)->resetLeaf("startyear", "1935");
      (*m)->resetLeaf("endyear", "2099"); 
      Object* officerName = new Object("name");
      officerNames->setValue(officerName);
      officerName->setLeaf("name", (*m)->safeGetString("name"));
      officerName->setLeaf("picture", (*m)->safeGetString("picture")); 
      
      Object* curr = currGenerals; 
      vector<objvec>* theVector = &generals;
      if ((*m)->safeGetString("category") == "admiral") {
	curr = currAdmirals; 
	theVector = &admirals;
      }
      else if ((*m)->safeGetString("category") == "commander") {
	curr = currCommders; 
	theVector = &commders; 
      }
      curr->resetLeaf("total", curr->safeGetInt("total") + 1);
      curr->resetLeaf((*m)->safeGetString("rank"), curr->safeGetInt((*m)->safeGetString("rank")) + 1);
      (*theVector)[(*m)->safeGetInt("rank")].push_back(*m);
    }
    (*hoi)->unsetValue("leader");
    
    currGenerals->resetLeaf("tag", (*hoi)->safeGetString("tag"));
    currAdmirals->resetLeaf("tag", (*hoi)->safeGetString("tag"));
    currCommders->resetLeaf("tag", (*hoi)->safeGetString("tag"));    
   
    generalRanks.push_back(currGenerals);
    admiralRanks.push_back(currAdmirals);
    commderRanks.push_back(currCommders);

    Logger::logStream(DebugLeaders) << "  Distribution: "
				    << currGenerals->safeGetInt("total") << " " << currGenerals->safeGetString("3") << " " 
      				    << currAdmirals->safeGetInt("total") << " " << currAdmirals->safeGetString("3") << " " 
      				    << currCommders->safeGetInt("total") << " " << currCommders->safeGetString("3") << " " 
				    << "\n"; 
    

    objvec curr_teams = (*hoi)->getValue("tech_team");
    Logger::logStream(DebugTechTeams) << "Tech teams from " << (*hoi)->safeGetString("tag") << ":\n";
    for (objiter t = curr_teams.begin(); t != curr_teams.end(); ++t) {
      Object* id = (*t)->safeGetObject("id");
      if (!id) continue;
      int team_id = id->safeGetInt("id", -1);
      if (-1 == team_id) continue;
      int skill = (*t)->safeGetInt("skill");
      if (team_id >= (int) techTeams.size()) techTeams.resize(team_id+1);
      techTeams[team_id] = (*t);
      (*t)->resetLeaf("startyear", "1935");
      (*t)->unsetValue("endyear");
      (*t)->setLeaf("endyear", "2999");
      (*t)->setLeaf("endyear", "2999");       
      Logger::logStream(DebugTechTeams) << "  " << (*t)->safeGetString("name") << " id " << team_id << " skill " << skill;
      Object* rtypes = (*t)->safeGetObject("research_types");
      for (int r = 0; r < rtypes->numTokens(); ++r) {
	Logger::logStream(DebugTechTeams) << " " << rtypes->getToken(r);
      }
      Logger::logStream(DebugTechTeams) << "\n"; 
    }
    (*hoi)->unsetValue("tech_team");

    Object* vicCountry = findVicCountryByHoiCountry(*hoi);
    if (vicCountry) {
      Object* topcustom = customObject->safeGetObject(vicCountry->getKey());
      if (topcustom) {
	Object* custom = topcustom->safeGetObject("customTechTeams");
	if (custom) (*hoi)->setValue(custom);
	custom = topcustom->safeGetObject("customOfficers");
	if (custom) (*hoi)->setValue(custom);
	custom = topcustom->safeGetObject("customMinisters");
	if (custom) (*hoi)->setValue(custom); 
      }

      int milsize = 0;
      objvec armies = vicCountry->getValue("army");
      for (objiter army = armies.begin(); army != armies.end(); ++army) {
	objvec regiments = (*army)->getValue("regiment");
	milsize += regiments.size();
      }
      (*hoi)->setLeaf("vicmilsize", milsize);
      (*hoi)->setLeaf("ruling_party", vicCountry->safeGetString("ruling_party", "-1")); 
    }
  }

  Logger::logStream(DebugUnits) << "Input HoI units: \n";
  for (map<string, int>::iterator i = landdivs.begin(); i != landdivs.end(); ++i) {
    Logger::logStream(DebugUnits) << "  LD: " << (*i).first << " : " << (*i).second << "\n";
  }
  for (map<string, int>::iterator i = landbrigs.begin(); i != landbrigs.end(); ++i) {
    Logger::logStream(DebugUnits) << "  LB: " << (*i).first << " : " << (*i).second << "\n";
  }
  for (map<string, int>::iterator i = navaldivs.begin(); i != navaldivs.end(); ++i) {
    Logger::logStream(DebugUnits) << "  ND: " << (*i).first << " : " << (*i).second << "\n";
  }
  for (map<string, int>::iterator i = navalbrigs.begin(); i != navalbrigs.end(); ++i) {
    Logger::logStream(DebugUnits) << "  NB: " << (*i).first << " : " << (*i).second << "\n";
  }
  for (map<string, int>::iterator i = airdivs.begin(); i != airdivs.end(); ++i) {
    Logger::logStream(DebugUnits) << "  AD: " << (*i).first << " : " << (*i).second << "\n";
  }
  for (map<string, int>::iterator i = airbrigs.begin(); i != airbrigs.end(); ++i) {
    Logger::logStream(DebugUnits) << "  AB: " << (*i).first << " : " << (*i).second << "\n";
  }

  Object* modifiers = loadTextFile(sourceVersion + "event_modifiers.txt"); 
  
  for (objiter vic = vicProvinces.begin(); vic != vicProvinces.end(); ++vic) {
    objvec pops = (*vic)->getLeaves();
    for (objiter pop = pops.begin(); pop != pops.end(); ++pop) {
      if (0 > (*pop)->safeGetInt("size", 0)) continue;
      if (0 == (*pop)->safeGetObject("ideology")) continue;
      string id = (*pop)->safeGetString("id");
      idToPopMap[id] = (*pop); 
    }
    objvec mods = (*vic)->getValue("modifier");
    double rgoProdMod = 1;

    bool debug = ((*vic)->safeGetString("debug", "no") == "yes"); 
    bool farmers = (0 < (*vic)->getValue("farmers").size());
    if (debug) Logger::logStream(DebugResources) << "RGO mods for province " << (*vic)->getKey(); 
    for (objiter mod = mods.begin(); mod != mods.end(); ++mod) {
      string modname = remQuotes((*mod)->safeGetString("modifier"));
      Object* modObject = modifiers->safeGetObject(modname);
      if (!modObject) {
	Logger::logStream(Logger::Warning) << "Warning: Could not find province modifier " << modname << "\n";
	continue;
      }
      double effmod = modObject->safeGetFloat(farmers ? "farm_RGO_eff" : "mine_RGO_eff");
      rgoProdMod += effmod;
      if (debug) Logger::logStream(DebugResources) << " (" << modname << " " << effmod << ")"; 
    }
    // TODO 2: Tech effects on production
    double numAristos = (*vic)->safeGetFloat("aristocratmod");
    rgoProdMod += numAristos; 
    if (debug) Logger::logStream(DebugResources) << " (Aristocrats " << numAristos << ") Total: " << rgoProdMod << "\n"; 
    
    if (rgoProdMod < 0.1) rgoProdMod = 0.1; 

    
    // Set employment weights
    Object* rgo = (*vic)->safeGetObject("rgo");
    rgo->resetLeaf("prodMod", rgoProdMod);
    popMerger->setFactoryEmployed(rgo, remQuotes(rgo->safeGetString("goods_type"))); 
  }


  // Preliminary factory iteration to find diversity and profit 
  Object* factoryProduction = configObject->safeGetObject("production");
  double maxProfit = 0;
  double maxGdp = 0;   
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {
    objvec states = (*vic)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      objvec buildings = (*state)->getValue("state_buildings");
      for (objiter b = buildings.begin(); b != buildings.end(); ++b) {
	string btype = remQuotes((*b)->safeGetString("building"));	
	string goods = factoryProduction->safeGetString(btype, "nothing");
	if (goods == "nothing") Logger::logStream(Logger::Warning) << "Warning: Could not find production for factory " << btype << "\n"; 

	double avgProfit = 0;
	Object* profits = (*b)->safeGetObject("profit_history_entry");
	if (profits) {
	  for (int i = 0; i < profits->numTokens(); ++i) {
	    avgProfit += atof(profits->getToken(i).c_str());
	  }
	}
	if ((profits) && (0 < profits->numTokens())) avgProfit /= profits->numTokens();
	if (avgProfit > 0) {
	  incomeMap[*vic][goods] += avgProfit;
	  incomeMap[*vic]["totalPositive"] += avgProfit;
	}
	incomeMap[*vic]["total"] += avgProfit;
	if (incomeMap[*vic]["total"] > maxGdp) maxGdp = incomeMap[*vic]["total"]; 	
	if (avgProfit > maxProfit) maxProfit = avgProfit;
	(*b)->resetLeaf("avgProfit", avgProfit);

	Object* employment = (*b)->safeGetObject("employment");
	if (!employment) continue;
	employment = employment->safeGetObject("employees");
	if (!employment) continue;
	objvec emps = employment->getLeaves();
	
	for (objiter emp = emps.begin(); emp != emps.end(); ++emp) {
	  incomeMap[*vic]["workers"] += (*emp)->safeGetFloat("count");
	}
      }
    }
  }

  double maxEfficiency = 0;
  for (map<Object*, map<string, double> >::iterator i = incomeMap.begin(); i != incomeMap.end(); ++i) {
    double eff = (*i).second["total"];
    if (eff < 0.05*maxGdp) continue; 
    eff /= (*i).second["workers"];
    if (eff < maxEfficiency) continue;
    maxEfficiency = eff; 
  }

  double diversityBonus  = configObject->safeGetFloat("diversityBonus"); 
  double efficiencyBonus = configObject->safeGetFloat("efficiencyBonus");
  Logger::logStream(DebugResources) << "Diversity and efficiency: " << maxEfficiency << "\n"; 
  for (map<Object*, map<string, double> >::iterator i = incomeMap.begin(); i != incomeMap.end(); ++i) {
    Object* vic = (*i).first;
    int numSectors = 0;
    double gdp = (*i).second["totalPositive"]; 
    for (map<string, double>::iterator g = (*i).second.begin(); g != (*i).second.end(); ++g) {
      if ((*g).first == "totalPositive") continue;
      if ((*g).first == "total") continue;
      if ((*g).first == "workers") continue;      
      if ((*g).second / gdp < 0.05) continue;
      numSectors++; 
    }
    vic->resetLeaf("diversity", 1 + diversityBonus*numSectors);
    double eff = incomeMap[vic]["total"];
    eff /= incomeMap[vic]["workers"];    
    eff /= maxEfficiency;
    if (eff > 1) eff = 1;
    if (eff < -0.5) eff = -0.5; 
    vic->resetLeaf("efficiency", (1 + efficiencyBonus * eff)); 
    Logger::logStream(DebugResources) << "  " << vic->getKey() << " : "
				      << (1 + diversityBonus * numSectors) << " "
				      << (1 + efficiencyBonus * eff) << " (from "
				      << (*i).second["total"] << " "
				      << (*i).second["workers"] << " "
				      << eff << " "
				      << (incomeMap[vic]["total"] / incomeMap[vic]["workers"])
				      << ")\n"; 
  }

  
  
  // Set employment weights for factory POPs  
  for (objiter vic = vicCountries.begin(); vic != vicCountries.end(); ++vic) {

    double diversity = (*vic)->safeGetFloat("diversity", 1);
    double efficiency = (*vic)->safeGetFloat("efficiency", 1);
    double prodMod = 1;
    prodMod *= diversity;
    prodMod *= efficiency; 
    if (1 != prodMod) Logger::logStream(DebugResources) << (*vic)->getKey() << " prodmod: " << prodMod << "\n"; 
    
    objvec states = (*vic)->getValue("state");
    for (objiter state = states.begin(); state != states.end(); ++state) {
      objvec buildings = (*state)->getValue("state_buildings");
      for (objiter b = buildings.begin(); b != buildings.end(); ++b) {
	string btype = remQuotes((*b)->safeGetString("building"));	
	string goods = factoryProduction->safeGetString(btype, "nothing");
	double unprofit = (*b)->safeGetFloat("unprofitable_days");
	if (unprofit > 1000) unprofit = 1000; 
	unprofit = (1 - 0.0002*unprofit); 
	
	(*b)->resetLeaf("prodMod", prodMod*unprofit); 
	double curr = popMerger->setFactoryEmployed(*b, goods);
	(*state)->resetLeaf("totalFactoryWorkers", curr + (*state)->safeGetFloat("totalFactoryWorkers"));
	(*state)->resetLeaf("totalFactoryCapacity", (*state)->safeGetInt("totalFactoryCapacity") + 10000 * (*b)->safeGetInt("level")); 
      }
    }
  }

  Object* goodsWeights = configObject->safeGetObject("goodsWeights");
  assert(goodsWeights);
  hoiProducts.push_back("industry");
  hoiProducts.push_back("oil");
  hoiProducts.push_back("metal");
  hoiProducts.push_back("energy");  
  hoiProducts.push_back("rares");

  
  objvec goods = goodsWeights->getLeaves();
  for (objiter g = goods.begin(); g != goods.end(); ++g) {
    double total = 0;
    vector<string> negs;
    map<string, double> numbers; 
    for (vector<string>::iterator hp = hoiProducts.begin(); hp != hoiProducts.end(); ++hp) {
      double curr = (*g)->safeGetFloat(*hp);
      if (curr < 0) negs.push_back(*hp); 
      else total += curr;
      numbers[*hp] = curr; 
    }
    for (vector<string>::iterator n = negs.begin(); n != negs.end(); ++n) {
      Logger::logStream(Logger::Warning) << "Resetting " << (*n) << " weight of "
					 << (*g)->getKey() << " to zero.\n"; 
      (*g)->resetLeaf((*n), "0");
    }
    if (total > 0.99) {
      Logger::logStream(Logger::Warning) << "Rescaling weights of " << (*g)->getKey() << ".\n"; 
      for (map<string, double>::iterator i = numbers.begin(); i != numbers.end(); ++i) {
	double curr = (*i).second;
	curr /= total;
	curr *= 0.99;
	(*g)->resetLeaf((*i).first, curr); 
      }
    }
    (*g)->resetLeaf("manpower", 1 - total); 
  }

  hoiProducts.insert(hoiProducts.begin() + 1, "manpower");
}

void resetLeaves (Object* dat, string value) {
  objvec leaves = dat->getLeaves();
  for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
    dat->resetLeaf((*l)->getKey(), value); 
  }
}

void resetLists (Object* dat) {
  objvec leaves = dat->getLeaves();
  for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
    (*l)->clear();
    (*l)->addToList("0"); 
  }
}

void fixUpgrades (Object* upgrades, string quantity, double def, double value, vector<string>& units); 

vector<string> landunits;
vector<string> navalunits;
vector<string> airunits;

void fixUpgrades (Object* upgrades, string quantity, double def, double value, string unit) {
  if      (unit == "land")  fixUpgrades(upgrades, quantity, def, value, landunits);
  else if (unit == "naval") fixUpgrades(upgrades, quantity, def, value, navalunits);
  else if (unit == "air")   fixUpgrades(upgrades, quantity, def, value, airunits);
  else {
    Object* u = upgrades->safeGetObject(unit);
    if (!u) {
      u = new Object(upgrades->safeGetObject(unit));
      upgrades->setValue(u);
    }
    double newValue = u->safeGetFloat(quantity, def) + value;
    u->resetLeaf(quantity, newValue);
    //Logger::logStream(DebugTech) << "Increased " << quantity << " to " << newValue << " for " << unit << "\n"; 
  }
}

void fixUpgrades (Object* upgrades, string quantity, double def, double value, vector<string>& units) {
  for (vector<string>::iterator u = units.begin(); u != units.end(); ++u) fixUpgrades(upgrades, quantity, def, value, (*u)); 
}

void resetObject (Object* target, Object* source) {
  objvec evts = source->getLeaves();
  for (objiter evt = evts.begin(); evt != evts.end(); ++evt) {
    target->resetLeaf((*evt)->getKey(), (*evt)->getLeaf()); 
  }
}

Object* defaultObject = 0;
double aheadOfTimePenalty = 1.15;
objvec hoiTechs;
map<string, double> countriesWithTech;
map<string, double> countriesWantTech;
bool WorkerThread::recursiveBuy (string techid, Object* hoiCountry, double& points, map<string, bool>& gotTechs, Object* vicCountry) {
 
  if (gotTechs[techid]) return true; 
  Object* currTech = 0;
  for (objiter ht = hoiTechs.begin(); ht != hoiTechs.end(); ++ht) {
    if (techid != (*ht)->safeGetString("id")) continue;
    currTech = (*ht);
    break; 
  }
  if (!currTech) {
    Logger::logStream(Logger::Warning) << "Warning: Cannot find tech " << techid << ", wanted by " << hoiCountry->safeGetString("tag") << "\n";
    return false; 
  }
  
  vector<string> prereqs; 
  Object* required = currTech->safeGetObject("required"); 
  if (required) {
    for (int j = 0; j < required->numTokens(); ++j) {
      if (gotTechs[required->getToken(j)]) continue;
      prereqs.push_back(required->getToken(j));
    }
  }

  bool canDo = true;
  for (vector<string>::iterator p = prereqs.begin(); p != prereqs.end(); ++p) {
    bool got = recursiveBuy((*p), hoiCountry, points, gotTechs, vicCountry);
    if (!got) canDo = false; 
  }
  
  if (!canDo) {
    Logger::logStream(Logger::Warning) << "Warning: " << hoiCountry->safeGetString("tag") << " cannot into tech " << techid << " 'cause no haz prereqs :( \n";
    return false; 
  }

  Object* disallow = hoiCountry->safeGetObject("deactivate");
  for (int dis = 0; dis < disallow->numTokens(); ++dis) {
    if (disallow->getToken(dis) != techid) continue;
    Logger::logStream(Logger::Warning) << "Warning: " << hoiCountry->safeGetString("tag")
				       << " cannot buy disallowed tech " << techid << "\n";
    return false; 
  }
  
  double cost = countriesWantTech[techid] / max(0.1, countriesWithTech[techid]);
  int year = currTech->safeGetInt("year");
  year -= 1936;
  if (0 > year) year = 0; 
  cost *= pow(aheadOfTimePenalty, year); 
  
  objvec comps = currTech->getValue("component");
  vector<pair<string, double> > bonuses;
  double overallBonus = 1; 
  for (objiter comp = comps.begin(); comp != comps.end(); ++comp) {
    string currType = (*comp)->safeGetString("type");
    double bonus = vicCountry->safeGetFloat(currType);
    if (0 == bonus) continue;
    bonuses.push_back(pair<string, double>(currType, bonus));
    cost *= (1 - bonus);
    overallBonus *= bonus; 
  }
  
  /*
    Logger::logStream(DebugTech) << "Country " <<  hoiCountry->safeGetString("tag") << " "
    << " wants to buy "
    << techid
    << " at cost "
    << cost << " = (1 - " << overallBonus << ") * ("
    << aheadOfTimePenalty << "^" << year << " = " << pow(aheadOfTimePenalty, year) << ") * " 
    << countriesWantTech[techid] << " / " 
    << countriesWithTech[techid] << ".\n";
  */

  if (cost > points) return false; 
  
  Logger::logStream(DebugTech) << "Country "
			       << hoiCountry->safeGetString("tag") 
			       << " buys tech "
			       << techid << " at cost "
			       << cost << " ("
			       << countriesWantTech[techid] << " " << countriesWithTech[techid] << " "
			       << year;
  for (unsigned int i = 0; i < bonuses.size(); ++i) Logger::logStream(DebugTech) << " " << bonuses[i].first << " " << bonuses[i].second;
  Logger::logStream(DebugTech) << ") from reserves "
			       << points
			       << ".\n";
  points -= cost;
      
  gotTechs[techid] = true; 

  Object* techlist = hoiCountry->safeGetObject("techapps");  
  techlist->addToList(techid);
  Object* effects = currTech->safeGetObject("effects");
  if (effects) {
    Object* upgrade            = hoiCountry->safeGetObject("upgrade");
    Object* models             = hoiCountry->safeGetObject("models");
    Object* obsolete_models    = hoiCountry->safeGetObject("obsolete_models");
    Object* allowed_divisions  = hoiCountry->safeGetObject("allowed_divisions");
    Object* allowed_brigades   = hoiCountry->safeGetObject("allowed_brigades");
    Object* allowed_buildings  = hoiCountry->safeGetObject("allowed_buildings");
    Object* building_prod_mod  = hoiCountry->safeGetObject("building_prod_mod");
    Object* building_eff_mod   = hoiCountry->safeGetObject("building_eff_mod");
    Object* convoy_prod_mod    = hoiCountry->safeGetObject("convoy_prod_mod");
    Object* mission_efficiency = hoiCountry->safeGetObject("mission_efficiency");
    Object* max_positioning    = hoiCountry->safeGetObject("max_positioning");
    Object* min_positioning    = hoiCountry->safeGetObject("min_positioning");
    Object* modifiers          = hoiCountry->safeGetObject("modifiers");
    Object* divs               = defaultObject->safeGetObject("allowed_divisions"); 
    Object* eventObject        = defaultObject->safeGetObject("events");
    
    objvec commands = effects->getValue("command");
    for (objiter c = commands.begin(); c != commands.end(); ++c) {
      string comm = (*c)->safeGetString("type", "BLAH");
      if (comm == "deactivate") disallow->addToList((*c)->safeGetString("which"));
      else if (comm == "activate_unit_type") {
	string unit = (*c)->safeGetString("which", "NOTHING");
	if (divs->safeGetString(unit, "BLAH") != "BLAH") allowed_divisions->resetLeaf(unit, "yes");
	else allowed_brigades->resetLeaf(unit, "yes");
      }
      else if (comm == "new_model") {
	string unittype = (*c)->safeGetString("which"); 
	Object* mod = models->safeGetObject(unittype);
	if (mod) {
	  string newmodel = (*c)->safeGetString("value");
	  if (newmodel != "0") mod->addToList(newmodel);	      
	}
	else {
	  Logger::logStream(Logger::Error) << "Error: Could not find unit type "
					   << unittype 
					   << ", ignoring effect of tech "
					   << techid
					   << "\n"; 
	}
      }
      else if (comm == "scrap_model") {
	string which = (*c)->safeGetString("which");
	Object* mod = obsolete_models->safeGetObject(which);
	if (!mod) {
	  mod = new Object(which);
	  mod->setObjList(true); 
	  obsolete_models->setValue(mod);
	}
	string newmodel = (*c)->safeGetString("value");
	if (newmodel != "0") mod->addToList(newmodel); 	    
      }
      else if (comm == "allow_building") {
	allowed_buildings->resetLeaf((*c)->safeGetString("which"), "yes"); 
      }
      else if (comm == "AA_batteries") {
	hoiCountry->resetLeaf("AA_efficiency", hoiCountry->safeGetFloat("AA_efficiency", 0.10) + 0.01*(*c)->safeGetFloat("value", 0)); 
      }
      else if (comm == "relative_manpower") { 
	hoiCountry->resetLeaf("relative_manpower", hoiCountry->safeGetFloat("relative_manpower", 0.95) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else if (comm == "BLAH") {} 
      else if ((comm == "research_mod") || (comm == "tc_mod") || (comm == "tc_occupied_mod") || (comm == "supply_dist_mod")) {
	hoiCountry->resetLeaf(comm, hoiCountry->safeGetFloat(comm, 1.0) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else if (comm == "convoy_def_eff") {
	hoiCountry->resetLeaf("convoy_def_eff", hoiCountry->safeGetFloat("convoy_def_eff", 0.0) + (*c)->safeGetFloat("value")); 
      }
      else if (comm == "repair_mod") {
	hoiCountry->resetLeaf("reinforcement", hoiCountry->safeGetFloat("reinforcement", 0.0) + 0.01*(*c)->safeGetFloat("value"));
      }
      else if (comm == "hq_supply_eff") {
	hoiCountry->resetLeaf("hq_supply_eff", hoiCountry->safeGetFloat("hq_supply_eff", 0.01) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else if (comm == "radar_eff") {
	hoiCountry->resetLeaf("radar_eff", hoiCountry->safeGetFloat("radar_eff", 1) + 0.01*(*c)->safeGetFloat("value")); 
      }	  
      else if (comm == "industrial_modifier") {
	string which = (*c)->safeGetString("which");
	if (which == "total") {
	  hoiCountry->resetLeaf("industrial_modifier", hoiCountry->safeGetFloat("industrial_modifier", 1) + 0.01*(*c)->safeGetFloat("value"));
	}
	else if (which == "gearing_limit") {
	  hoiCountry->resetLeaf("gearing_limit", hoiCountry->safeGetFloat("gearing_limit", 0.50) + 0.01*(*c)->safeGetFloat("value")); 
	}
	else if (which == "supplies") {
	  hoiCountry->resetLeaf("supply_modifier", hoiCountry->safeGetFloat("supply_modifier", 0.0) + 0.01*(*c)->safeGetFloat("value")); 
	}	    
      }
      else if ((comm == "allow_dig_in") || (comm == "allow_convoy_escorts")) {
	hoiCountry->resetLeaf(comm, "yes"); 
      }
      else if (comm == "build_time") {
	fixUpgrades(upgrade, "buildtime", 0, 0.01*(*c)->safeGetFloat("value"), (*c)->safeGetString("which"));
      }
      else if (comm == "build_cost") {
	fixUpgrades(upgrade, "cost", 0, 0.01*(*c)->safeGetFloat("value"), (*c)->safeGetString("which"));
      }
      else if (comm == "morale") {
	fixUpgrades(upgrade, "morale", 0, (*c)->safeGetFloat("value"), (*c)->safeGetString("which"));
      }
      else if (comm == "max_organization") {
	fixUpgrades(upgrade, "defaultorganisation", 0, (*c)->safeGetFloat("value"), (*c)->safeGetString("which"));
      }
      else if (comm == "max_positioning") {
	max_positioning->resetLeaf((*c)->safeGetString("which"), max_positioning->safeGetFloat((*c)->safeGetString("which")) + (*c)->safeGetFloat("value"));
      }
      else if (comm == "min_positioning") {
	min_positioning->resetLeaf((*c)->safeGetString("which"), min_positioning->safeGetFloat((*c)->safeGetString("which")) + (*c)->safeGetFloat("value"));
      }
      else if (comm == "task_efficiency") {
	mission_efficiency->resetLeaf((*c)->safeGetString("which"), mission_efficiency->safeGetFloat((*c)->safeGetString("which")) + (*c)->safeGetFloat("value"));
      }
      else if (comm == "nuclear_carrier") {
	if ((*c)->safeGetString("which") == "flying_bomb") hoiCountry->resetLeaf("nuclear_carrier", "yes");
	else hoiCountry->resetLeaf("missile_carrier", "yes");
      }
      else if ((getField(comm, 1, '_') == "defense") || (getField(comm, 1, '_') == "attack") || (getField(comm, 1, '_') == "move")) {
	Object* unitmod = modifiers->safeGetObject((*c)->safeGetString("which"));
	unitmod->resetLeaf(comm, unitmod->safeGetInt(comm) + (*c)->safeGetInt("value")); 
      }
      else if ((comm == "attrition_mod") || (comm == "trickleback_mod")) {
	hoiCountry->resetLeaf(comm, hoiCountry->safeGetFloat(comm, 1) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else if (comm == "enable_task") {
	hoiCountry->resetLeaf((*c)->safeGetString("which"), "yes"); 
      }
      else if (eventObject->safeGetString(comm, "BLAH") != "BLAH") {
	hoiCountry->resetLeaf(comm, hoiCountry->safeGetFloat(comm, 0.01) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else if (comm == "speed") {
	Object* up = upgrade->safeGetObject((*c)->safeGetString("which"));
	up->resetLeaf("maxspeed", up->safeGetFloat(comm, 2) + (*c)->safeGetFloat("value")); 
      }
      else if ((comm == "speed_cap_art") || (comm == "speed_cap_at") || (comm == "speed_cap_aa")) {
	Object* up = upgrade->safeGetObject((*c)->safeGetString("which"));
	up->resetLeaf(comm, up->safeGetFloat(comm, 1) + (*c)->safeGetFloat("value")); 
      }
      else if (comm == "sce_frequency") {
	hoiCountry->resetLeaf(comm, hoiCountry->safeGetFloat(comm, 1) + (*c)->safeGetFloat("value")); 
      }
      else if ((comm == "max_amphib_mod") || (comm == "max_reactor_size")) {
	hoiCountry->resetLeaf(comm, (*c)->safeGetInt("value")); 
      }
      else if (comm == "building_eff_mod") {
	string target = (*c)->safeGetString("which");
	building_eff_mod->resetLeaf(target, building_eff_mod->safeGetFloat(target) + 0.01*(*c)->safeGetFloat("value"));
      }
      else if (comm == "building_prod_mod") {
	string target = (*c)->safeGetString("which");
	building_prod_mod->resetLeaf(target, building_prod_mod->safeGetFloat(target) + 0.01*(*c)->safeGetFloat("value"));
      }
      else if (comm == "industrial_multiplier") {
	hoiCountry->resetLeaf((*c)->safeGetString("which"), (*c)->safeGetString("value")); 
      }
      else if (comm == "surprise") {
	string which = (*c)->safeGetString("which") + "_surprise";
	hoiCountry->resetLeaf(which, hoiCountry->safeGetFloat(which, 1) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else if (comm == "intelligence") {
	if ((*c)->safeGetString("which") == "us") 
	  hoiCountry->resetLeaf("intelligence", hoiCountry->safeGetFloat("intelligence") + 0.01*(*c)->safeGetFloat("value"));
	else
	  hoiCountry->resetLeaf("enemy_intelligence", hoiCountry->safeGetFloat("enemy_intelligence") + 0.01*(*c)->safeGetFloat("value"));
      }
      else if (comm == "army_detection") {
	if ((*c)->safeGetString("which") == "us") 
	  hoiCountry->resetLeaf("army_detection", hoiCountry->safeGetFloat("army_detection") + 0.01*(*c)->safeGetFloat("value"));
	else
	  hoiCountry->resetLeaf("enemy_army_detection", hoiCountry->safeGetFloat("enemy_army_detection") + 0.01*(*c)->safeGetFloat("value"));
      }
      else if (comm == "convoy_prod_mod") {
	convoy_prod_mod->resetLeaf((*c)->safeGetString("which"), convoy_prod_mod->safeGetFloat((*c)->safeGetString("which")) + 0.01*(*c)->safeGetFloat("value")); 
      }
      else {
	Logger::logStream(Logger::Warning) << "Unknown tech command " << comm << "\n"; 
      }
    }
  }
  return true; 
}

void WorkerThread::techs () {
  landunits.push_back("infantry");
  landunits.push_back("cavalry");
  landunits.push_back("motorized");
  landunits.push_back("mechanized");
  landunits.push_back("light_armor");
  landunits.push_back("armor");
  landunits.push_back("paratrooper");
  landunits.push_back("marine");
  landunits.push_back("bergsjaeger");
  landunits.push_back("garrison");
  landunits.push_back("hq");
  landunits.push_back("militia");
  airunits.push_back("multi_role");
  airunits.push_back("interceptor");
  airunits.push_back("strategic_bomber");
  airunits.push_back("tactical_bomber");
  airunits.push_back("naval_bomber");
  airunits.push_back("cas");
  airunits.push_back("transport_plane");
  airunits.push_back("flying_bomb");
  airunits.push_back("flying_rocket");
  navalunits.push_back("battleship");
  navalunits.push_back("light_cruiser");
  navalunits.push_back("heavy_cruiser");
  navalunits.push_back("battlecruiser");
  navalunits.push_back("destroyer");
  navalunits.push_back("carrier");
  navalunits.push_back("escort_carrier");
  navalunits.push_back("submarine");
  navalunits.push_back("nuclear_submarine");
  navalunits.push_back("transport"); 
  
  Object* htechobj = loadTextFile(targetVersion + "techs.txt");
  objvec hoiTechAreas  = htechobj->getValue("technology");
  for (objiter hta = hoiTechAreas.begin(); hta != hoiTechAreas.end(); ++hta) {
    objvec apps = (*hta)->getValue("application");
    for (objiter a = apps.begin(); a != apps.end(); ++a) hoiTechs.push_back(*a); 
  }
  Object* vtechobj = loadTextFile(sourceVersion + "victechs.txt");
  objvec victechs  = vtechobj->getLeaves(); 

  defaultObject = loadTextFile(targetVersion + "defaults.txt");
  assert(defaultObject); 
  
  double totalHoiTechs = 0;
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* techlist = (*hoi)->safeGetObject("techapps");
    if (!techlist) continue;
    for (int i = 0; i < techlist->numTokens(); ++i) {
      countriesWithTech[techlist->getToken(i)]++;
      totalHoiTechs++; 
    }
  }
 
  double totalVicRPs = 0; 
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* vicCountry = findVicCountryByHoiCountry(*hoi);
    if (!vicCountry) continue;
    
    Object* custom = customObject->safeGetObject(vicCountry->getKey());
    if (custom) {
      custom = custom->safeGetObject("techs");
      if (!custom) {
	Logger::logStream(Logger::Error) << "Custom object but no techs for " << vicCountry->getKey() << "\n";
	continue; 
      }
    }
    else { 
      custom = customObject->safeGetObject("DUMMY");
      if (!custom) {
	custom = new Object("DUMMY");
	customObject->setValue(custom);
	Object* techs = new Object("techs");
	custom->setValue(techs); 
	techs->setObjList();
	techs->addToList("4010"); techs->addToList("4020"); techs->addToList("4120"); techs->addToList("4130"); techs->addToList("2150");
	techs->addToList("2010"); techs->addToList("2020"); techs->addToList("2030"); techs->addToList("2290"); techs->addToList("2300");
	techs->addToList("2310"); techs->addToList("2450"); techs->addToList("2460"); techs->addToList("2510"); techs->addToList("1010");
	techs->addToList("1020"); techs->addToList("1190"); techs->addToList("1200"); techs->addToList("1210"); techs->addToList("1220");
	techs->addToList("4220"); techs->addToList("1110"); techs->addToList("2400"); techs->addToList("1150"); techs->addToList("1310");
	techs->addToList("5010"); techs->addToList("5020"); techs->addToList("5050"); techs->addToList("5080"); techs->addToList("5090");
	techs->addToList("5190"); techs->addToList("5200"); techs->addToList("5310"); techs->addToList("5320"); techs->addToList("5410");
	techs->addToList("5470"); techs->addToList("5480"); techs->addToList("3420"); techs->addToList("9010"); techs->addToList("9020");
	techs->addToList("9030"); techs->addToList("9140"); techs->addToList("6010"); techs->addToList("6030"); techs->addToList("6110");
	techs->addToList("6120"); techs->addToList("8010"); techs->addToList("8020"); techs->addToList("8030"); techs->addToList("8040");
	techs->addToList("8050"); techs->addToList("8070"); techs->addToList("8100"); techs->addToList("8120"); techs->addToList("3010");
	techs->addToList("3020"); techs->addToList("3030"); techs->addToList("3070"); techs->addToList("3080"); techs->addToList("3090");
	techs->addToList("12100"); techs->addToList("12110"); techs->addToList("3100"); techs->addToList("3130"); techs->addToList("3140");
	techs->addToList("3150"); techs->addToList("3190"); techs->addToList("3200"); techs->addToList("3210"); techs->addToList("3250");
	techs->addToList("3260"); techs->addToList("3270"); techs->addToList("3280"); techs->addToList("3320"); techs->addToList("3330");
	techs->addToList("3340"); techs->addToList("3400"); techs->addToList("3410"); techs->addToList("14000"); techs->addToList("3470");
	techs->addToList("1380");
      }
      custom = custom->safeGetObject("techs");
    }
    assert(custom);
    for (int i = 0; i < custom->numTokens(); ++i) {
      countriesWantTech[custom->getToken(i)]++; 
    }

    Object* countryTechs = vicCountry->safeGetObject("technology");
    if (!countryTechs) continue;
    for (objiter vt = victechs.begin(); vt != victechs.end(); ++vt) {
      if (0 == countryTechs->safeGetObject((*vt)->getKey())) continue;
      int cost = (*vt)->safeGetInt("cost");
      totalVicRPs += cost; 
      vicCountry->resetLeaf("totalRPs", cost + vicCountry->safeGetInt("totalRPs"));
    }
  }

  aheadOfTimePenalty = 1 + configObject->safeGetFloat("aheadOfTimePenalty", 0.15); 
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* vicCountry = findVicCountryByHoiCountry(*hoi);
    if (!vicCountry) continue;

    (*hoi)->unsetValue("land_fort_eff");
    (*hoi)->unsetValue("coast_fort_eff");
    (*hoi)->unsetValue("ground_def_eff");
    (*hoi)->resetLeaf("convoy_def_eff", "0.000");
    (*hoi)->resetLeaf("energy_to_oil", "0.000");
    (*hoi)->resetLeaf("reinforcement", "0.000");
    (*hoi)->resetLeaf("oil_to_rare_materials", "0.000");
    (*hoi)->resetLeaf("industrial_modifier", "1.000");
    (*hoi)->resetLeaf("supply_modifier", "0.000");
    (*hoi)->resetLeaf("gearing_limit", "0.500");
    (*hoi)->resetLeaf("gearing_efficiency", "1.000");
    (*hoi)->unsetValue("minisub_bonus");
    (*hoi)->resetLeaf("AA_efficiency", "0.10");
    (*hoi)->unsetValue("carrier_level");
    (*hoi)->resetLeaf("intelligence", "0.5000");
    (*hoi)->resetLeaf("enemy_intelligence", "0.5000");
    (*hoi)->resetLeaf("army_detection", "0.5000");
    (*hoi)->resetLeaf("enemy_army_detection", "0.5000");
    (*hoi)->resetLeaf("land_surprise", "1.000");
    (*hoi)->resetLeaf("naval_surprise", "1.000");
    (*hoi)->resetLeaf("air_surprise", "1.000");
    (*hoi)->resetLeaf("hq_supply_eff", "0.01");
    (*hoi)->resetLeaf("sce_frequency", "1.000");
    (*hoi)->resetLeaf("allow_convoy_escorts", "no");
    (*hoi)->resetLeaf("radar_eff", "1.000"); 
    (*hoi)->unsetValue("max_reactor_size");
    (*hoi)->unsetValue("max_amphib_mod"); 
    (*hoi)->resetLeaf("research_mod", "1.000"); 
    (*hoi)->resetLeaf("relative_manpower", "0.95");
    (*hoi)->resetLeaf("trickleback_mod", "1.000");
    (*hoi)->resetLeaf("attrition_mod", "1.000");    
    (*hoi)->resetLeaf("tc_mod", "1.000");
    (*hoi)->resetLeaf("tc_occupied_mod", "1.000");
    (*hoi)->resetLeaf("supply_dist_mod", "1.000");    
    
    Object* upgrade            = (*hoi)->safeGetObject("upgrade");
    Object* models             = (*hoi)->safeGetObject("models");
    Object* obsolete_models    = (*hoi)->safeGetObject("obsolete_models");
    Object* allowed_divisions  = (*hoi)->safeGetObject("allowed_divisions");
    Object* allowed_brigades   = (*hoi)->safeGetObject("allowed_brigades");
    Object* allowed_buildings  = (*hoi)->safeGetObject("allowed_buildings");
    Object* building_prod_mod  = (*hoi)->safeGetObject("building_prod_mod");
    Object* building_eff_mod   = (*hoi)->safeGetObject("building_eff_mod");
    Object* convoy_prod_mod    = (*hoi)->safeGetObject("convoy_prod_mod");
    Object* mission_efficiency = (*hoi)->safeGetObject("mission_efficiency");
    Object* max_positioning    = (*hoi)->safeGetObject("max_positioning");
    Object* min_positioning    = (*hoi)->safeGetObject("min_positioning");
    Object* modifiers          = (*hoi)->safeGetObject("modifiers");

    objvec types = modifiers->getLeaves();
    Object* defMods = defaultObject->safeGetObject("modifiers");
    for (objiter t = types.begin(); t != types.end(); ++t) {
      Object* dt = defMods->safeGetObject((*t)->getKey());
      resetObject((*t), dt);
    }

    Object* eventObject = defaultObject->safeGetObject("events");
    resetObject((*hoi), eventObject); 
        
    Object* taskObject = defaultObject->safeGetObject("tasks");
    resetObject((*hoi), taskObject); 

    Object* defBuildProds = defaultObject->safeGetObject("buildingProds");
    resetObject(building_prod_mod, defBuildProds);

    Object* defBuildEffs = defaultObject->safeGetObject("buildingEffs");
    resetObject(building_eff_mod, defBuildEffs);

    Object* defMaxPos = defaultObject->safeGetObject("max_positioning");
    resetObject(max_positioning, defMaxPos);

    Object* defMinPos = defaultObject->safeGetObject("min_positioning");
    resetObject(min_positioning, defMinPos);

    Object* defMissEffs = defaultObject->safeGetObject("missions");
    resetObject(mission_efficiency, defMissEffs); 

    resetLeaves(convoy_prod_mod, "1.000"); 
    
    resetLeaves(allowed_divisions, "no");
    // No tech enables these two. 
    allowed_divisions->resetLeaf("militia", "yes");
    allowed_divisions->resetLeaf("transport", "yes"); 
    resetLeaves(allowed_brigades, "no");
    resetLeaves(allowed_buildings, "no");
    resetLists(models);
    resetLists(obsolete_models);
    models->safeGetObject("militia")->addToList("0"); 
    objvec units = upgrade->getLeaves();
    for (objiter unit = units.begin(); unit != units.end(); ++unit) {
      resetLeaves((*unit), "0"); 
    }    
    
    Object* techlist = (*hoi)->safeGetObject("techapps");
    if (!techlist) {
      techlist = new Object("techapps");
      techlist->setObjList(true); 
      (*hoi)->setValue(techlist);
    }
    techlist->clear(); 

    Object* disallow = (*hoi)->safeGetObject("deactivate"); 
    if (!disallow) {
      disallow = new Object("deactivate"); 
      disallow->setObjList(true); 
      (*hoi)->setValue(disallow);
    }
    disallow->clear(); 
  
    Object* custom = customObject->safeGetObject(vicCountry->getKey());
    Object* ministerCustom = 0; 
    if (custom) {
      ministerCustom = custom->safeGetObject("ministers");
      custom = custom->safeGetObject("techs");
      if (!custom) {
	Logger::logStream(Logger::Error) << "Custom object but no techs for " << vicCountry->getKey() << "\n";
	continue; 
      }
    }
    if (!custom) {
      custom = customObject->safeGetObject("DUMMY");
      custom = custom->safeGetObject("techs");
    }
    assert(custom);

    double points = vicCountry->safeGetFloat("totalRPs") / totalVicRPs;
    points *= totalHoiTechs; 

    if (ministerCustom) {
      objvec hoi_ministers = (*hoi)->getValue("minister"); 
      
      objvec leaves = ministerCustom->getLeaves();
      for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
	string govtype = (*l)->getKey();
	objvec poses = (*l)->getLeaves();
	Logger::logStream(DebugMinisters) << "Found " << govtype << " kustom for " << (*hoi)->safeGetString("tag") << " " << (int) poses.size() << "\n";
	for (objiter p = poses.begin(); p != poses.end(); ++p) {
	  string wantedPersonality = (*p)->safeGetString("personality", "nochange");
	  string wantedPosition = (*p)->getKey(); 
	  string newName = (*p)->safeGetString("name", "nochange");
	  string newPic = (*p)->safeGetString("picture", "nochange");
	  Object* cabinet = (*hoi)->safeGetObject(wantedPosition);
	  string wantedId = cabinet ? cabinet->safeGetString("id") : "nosuchcab"; 
	  if (govtype != (*hoi)->safeGetString("govType")) wantedId = "nosuchcab"; // Shadow governments have no ruling minister! 
	  
	  Logger::logStream(DebugMinisters) << "  Custom for " << wantedPosition << " " << newName << " " << newPic << " " << wantedPersonality << "\n"; 
	  
	  Object* existingPersonality = 0; 
	  if (wantedPersonality != "nochange") {
	    for (objiter m = hoi_ministers.begin(); m != hoi_ministers.end(); ++m) {
	      if ((*m)->safeGetString("position") != wantedPosition) continue;
	      if ((*m)->safeGetString("category") != govtype) continue;	      
	      if ((*m)->safeGetString("personality") != wantedPersonality) continue;
	      existingPersonality = (*m); 
	    }
	  }
	  
	  for (objiter m = hoi_ministers.begin(); m != hoi_ministers.end(); ++m) {
	    if ((*m)->safeGetString("position") != wantedPosition) continue;
	    if ((*m)->safeGetString("category") != govtype) continue;
	    if ((wantedId != "nosuchcab") && ((*m)->safeGetObject("id")->safeGetString("id") != wantedId)) continue; 
	    if (newPic != "nochange") (*m)->resetLeaf("picture", newPic);
	    if (newName != "nochange") (*m)->resetLeaf("name", newName);
	    if (wantedPersonality == "nochange") continue; 

	    if (existingPersonality) {
	      Logger::logStream(DebugMinisters) << "Country " << (*hoi)->safeGetString("tag")
						<< " already has a " << wantedPersonality << " "
						<< wantedPosition << " for " << govtype << ". No charge!\n";
	      existingPersonality->resetLeaf("personality", (*m)->safeGetString("personality"));
	      (*m)->resetLeaf("personality", wantedPersonality);
	      break; 
	    }
	    
	    double cost = personalities[wantedPosition]["total"] * configObject->safeGetFloat("ministerBaseCost", 2.0);
	    cost /= (cost + personalities[wantedPosition][wantedPersonality]);
	    if (cost > points) {
	      Logger::logStream(DebugMinisters) << "Country " << (*hoi)->safeGetString("tag") << " cannot afford to make "
						<< govtype << " " << wantedPosition << " " << wantedPersonality 
						<< " at cost " << cost << "\n";
	      break; 
	    }
	    Logger::logStream(DebugMinisters) << "Country " << (*hoi)->safeGetString("tag") << " makes " 
					      << govtype << " " << wantedPosition << " " << wantedPersonality
					      << " at cost " << cost << " from reserve " << points << "\n";
	    points -= cost;
	    (*m)->resetLeaf("personality", wantedPersonality);
	    break; 
	  }
	}
      }
    }
    
    map<string, bool> gotTechs; 
    for (int i = 0; i < custom->numTokens(); ++i) {
      string techid = custom->getToken(i);
      recursiveBuy(techid, (*hoi), points, gotTechs, vicCountry); 
    }
  }
}

int getModel (Object* country, string unit) {
  Object* models = country->safeGetObject("models");
  if (!models) return 0;
  models = models->safeGetObject(unit);
  if (!models) return 0;
  int ret = 0;
  for (int i = 0; i < models->numTokens(); ++i) {
    int curr = atoi(models->getToken(i).c_str());
    if (curr > ret) ret = curr;
  }
  return ret; 
}

Object* WorkerThread::findNavyLocation(Object* vicProv, vector<pair<Object*, string> >& fail, Object* hoiCountry) {
  if (!vicProv) return 0; 
  if (0 == vicProvinceToHoiProvincesMap[vicProv].size()) return 0;

  for (objiter cand = vicProvinceToHoiProvincesMap[vicProv].begin(); cand != vicProvinceToHoiProvincesMap[vicProv].end(); ++cand) {
    if (0 == (*cand)->safeGetObject("naval_base")) {
      fail.push_back(pair<Object*, string>((*cand), "No naval base"));
      continue;
    }
    if (hoiCountry->safeGetString("tag") != (*cand)->safeGetString("owner")) {
      fail.push_back(pair<Object*, string>((*cand), "Different owner"));
      continue;
    }
    if (hoiCountry->safeGetString("tag") != (*cand)->safeGetString("controller")) {
      fail.push_back(pair<Object*, string>((*cand), "Different controller"));
      continue;
    }
    return (*cand);
  }

  return 0; 
}

void WorkerThread::checkForExtra (Object* extraUnits, map<string, int>& totalVicRegiments, map<string, int>& existing) {
  if (!extraUnits) return;

  Logger::logStream(DebugUnits) << "Searching for extras in object " << extraUnits->getKey() << "\n";

  objvec extras = extraUnits->getLeaves();
  for (objiter extra = extras.begin(); extra != extras.end(); ++extra) {
    Logger::logStream(DebugUnits) << "Extra " << (*extra)->getKey() << " : ";      
    double vicRatio = totalVicRegiments[(*extra)->safeGetString("vicBonus")];
    vicRatio       /= totalVicRegiments[(*extra)->safeGetString("vicBase")];
    double vicMinimum = (*extra)->safeGetFloat("vicMinRatio");
    if (vicRatio < vicMinimum) {
      Logger::logStream(DebugUnits) << "None due to low ratio " << vicRatio << "\n";
      continue; 
    }
    double vicMaximum = (*extra)->safeGetFloat("vicMaxRatio");
    if (vicRatio > vicMaximum) vicRatio = vicMaximum;
    double hoiRatio = (*extra)->safeGetFloat("hoiRatio"); // This is the highest possible ratio
    Logger::logStream(DebugUnits) << "Vic ratio " << vicRatio
				  << " (from "
				  << totalVicRegiments[(*extra)->safeGetString("vicBonus")] << " / "
				  << totalVicRegiments[(*extra)->safeGetString("vicBase")] << ")";

    string basetype = (*extra)->safeGetString("hoiBase");
    int numBaseUnits = landdivs[basetype];
    if (0 == numBaseUnits) numBaseUnits = landbrigs[basetype];
    if (0 == numBaseUnits) numBaseUnits = navaldivs[basetype];
    if (0 == numBaseUnits) numBaseUnits = navalbrigs[basetype];
    if (0 == numBaseUnits) numBaseUnits = airdivs[basetype];
    if (0 == numBaseUnits) numBaseUnits = airbrigs[basetype];
    
    vicRatio -= vicMinimum;
    vicMaximum -= vicMinimum; 
    hoiRatio *= (vicRatio / vicMaximum);
    Logger::logStream(DebugUnits) << " gives HoI ratio " << hoiRatio;
    hoiRatio *= numBaseUnits; 
    int newNumber = (int) floor(hoiRatio + 0.5);
    Logger::logStream(DebugUnits) << " and therefore "
				  << newNumber
				  << " extra HoI divisions (based on "
				  << numBaseUnits
				  << " " << basetype << " in vanilla).\n"; 
    existing[(*extra)->getKey()] += newNumber; 
  }
}

void WorkerThread::units () {
  Object* unitInfo = loadTextFile(targetVersion + "unitInfo.txt"); 
  
  map<string, double> totalVicWeights;
  map<Object*, vector<string> > vicArmyNames;
  map<Object*, vector<string> > vicNavyNames;   
  map<string, int> totalVicRegiments;   
  Object* dummy = new Object("dummy");

  vector<pair<string, map<string, int>* > > extraNames;
  extraNames.push_back(pair<string, map<string, int>* >("extraDivs",    &landdivs));
  extraNames.push_back(pair<string, map<string, int>* >("extraBrigs",   &landbrigs));
  extraNames.push_back(pair<string, map<string, int>* >("extraShips",   &navaldivs));
  extraNames.push_back(pair<string, map<string, int>* >("extraAirDivs", &airdivs));
  for (vector<pair<string, map<string, int>* > >::iterator i = extraNames.begin(); i != extraNames.end(); ++i) {
    Object* extras = configObject->safeGetObject((*i).first);
    if (!extras) continue;
    objvec leaves = extras->getLeaves();
    for (objiter e = leaves.begin(); e != leaves.end(); ++e) {
      if ((*i).second->find((*e)->getKey()) == (*i).second->end()) (*(*i).second)[(*e)->getKey()] = 0;
    }
  }

  
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* vicCountry = findVicCountryByHoiCountry(*hoi);
    if (!vicCountry) continue;

    double mountainBonus = (*hoi)->safeGetFloat("mountains", 0.0001);
    mountainBonus /= (*hoi)->safeGetFloat("numOwned", 0.0001);
    mountainBonus += 1;
    
    Object* unitWeights = vicCountry->safeGetObject("unitWeights");
    if (!unitWeights) {
      unitWeights = new Object("unitWeights");
      vicCountry->setValue(unitWeights); 
    }
    
    objvec armies = vicCountry->getValue("army");
    for (objiter army = armies.begin(); army != armies.end(); ++army) {
      objvec regs = (*army)->getValue("regiment");
      for (objiter reg = regs.begin(); reg != regs.end(); ++reg) {
	string regtype = (*reg)->safeGetString("type", "NONE");	
	Object* popInfo = (*reg)->safeGetObject("pop");
	if (popInfo) {
	  string popId = popInfo->safeGetString("id");
	  popInfo = idToPopMap[popId];
	  if (popInfo) {
	    if (popInfo->getKey() != "soldiers") regtype = "mobilised";
	    popInfo->resetLeaf("supportedRegiments", 1 + popInfo->safeGetInt("supportedRegiments"));
	  }
	  
	}

	Object* divWeights = unitInfo->safeGetObject(regtype, dummy);
	vicArmyNames[vicCountry].push_back((*reg)->safeGetString("name"));
	totalVicRegiments[regtype]++; 
	
	for (map<string, int>::iterator u = landdivs.begin(); u != landdivs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  if ((*u).first == "bergsjaeger") currWeight *= mountainBonus; 
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = landbrigs.begin(); u != landbrigs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = airdivs.begin(); u != airdivs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = airbrigs.begin(); u != airbrigs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = navaldivs.begin(); u != navaldivs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = navalbrigs.begin(); u != navalbrigs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}		
      }
    }

    objvec navies = vicCountry->getValue("navy");
    for (objiter navy = navies.begin(); navy != navies.end(); ++navy) {
      objvec regs = (*navy)->getValue("ship");
      for (objiter reg = regs.begin(); reg != regs.end(); ++reg) {
	string regtype = (*reg)->safeGetString("type", "NONE");
	Object* divWeights = unitInfo->safeGetObject(regtype, dummy);
	vicNavyNames[vicCountry].push_back((*reg)->safeGetString("name"));
	totalVicRegiments[regtype]++; 	
	vicCountry->resetLeaf("vic_transports", divWeights->safeGetFloat("convoy_weight") + vicCountry->safeGetFloat("vic_transports"));
	vicCountry->resetLeaf("vic_escorts", divWeights->safeGetFloat("escort_weight") + vicCountry->safeGetFloat("vic_escorts"));
	
	for (map<string, int>::iterator u = landdivs.begin(); u != landdivs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = landbrigs.begin(); u != landbrigs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = airdivs.begin(); u != airdivs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = airbrigs.begin(); u != airbrigs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = navaldivs.begin(); u != navaldivs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}
	for (map<string, int>::iterator u = navalbrigs.begin(); u != navalbrigs.end(); ++u) {
	  double currWeight = divWeights->safeGetFloat((*u).first);
	  totalVicWeights[(*u).first] += currWeight;
	  unitWeights->resetLeaf((*u).first, currWeight + unitWeights->safeGetFloat((*u).first)); 
	}		
      }
    }
  }  

  for (vector<pair<string, map<string, int>* > >::iterator i = extraNames.begin(); i != extraNames.end(); ++i) {
    checkForExtra(configObject->safeGetObject((*i).first), totalVicRegiments, *((*i).second));
  }

  int numArmies = 1;
  int numRegiments = 1;
  int numAirUnits = 1;
  int numNavies = 1;
  map<string, double> overflow; 
  map<string, int> bonusLevel;
  Object* bonuses = configObject->safeGetObject("bonusLevels");
  if (bonuses) {
    objvec bonvec = bonuses->getLeaves();
    for (objiter b = bonvec.begin(); b != bonvec.end(); ++b) {
      bonusLevel[(*b)->getKey()] = atoi((*b)->getLeaf().c_str()); 
    }
  }

  /*
  Object* forbiddenArmies = configObject->safeGetObject("forbiddenArmies");
  vector<string> forbid;
  if (forbiddenArmies) {
    objvec leaves = forbiddenArmies->getValue("forbid"); 
    for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
      forbid.push_back((*l)->getLeaf()); 
    }
  }
  */
  
  for (objiter hoi = hoiCountries.begin(); hoi != hoiCountries.end(); ++hoi) {
    Object* vicCountry = findVicCountryByHoiCountry(*hoi);
    if (!vicCountry) continue;
    Object* unitWeights = vicCountry->safeGetObject("unitWeights");
    if (!unitWeights) continue; 
    
    objvec vArmies = vicCountry->getValue("army");
    objvec vicArmies;
    double totalArmyWeight = 0; 
    for (objiter varm = vArmies.begin(); varm != vArmies.end(); ++varm) {
      string vicLocation = (*varm)->safeGetString("location");
      Object* vLoc = vicGame->safeGetObject(vicLocation);
      assert(vLoc);
      if (0 == vicProvinceToHoiProvincesMap[vLoc].size()) continue; 
      Object* hLoc = vicProvinceToHoiProvincesMap[vLoc][0];
      Object* hInf = findHoiProvInfoFromHoiProvince(hLoc);
      if (!hInf) continue;
      if (hInf->safeGetString("terrain", "\"Ocean\"") == "\"Ocean\"") continue;
      
      if (hLoc->safeGetString("owner") != (*hoi)->safeGetString("tag")) {
	Object* actualOwner      = findHoiCountryByHoiTag(hLoc->safeGetString("owner"));
	Object* actualController = findHoiCountryByHoiTag(hLoc->safeGetString("controller"));
	bool isOk = false;
	if (hoiCountriesRelations[*hoi][actualOwner] & 1) { // War
	  if ((actualController == actualOwner) ||
	      (actualController == (*hoi)) ||
	      (hoiCountriesRelations[*hoi][actualController] & 2)) isOk = true;
	}
	
	if (hoiCountriesRelations[*hoi][actualController] & 2) isOk = true; // Mil-access 

	if (!isOk) continue;
      }
      
      vicArmies.push_back(*varm);
      double locWeight = 1;
      if (0 != hInf->safeGetInt("seazone")) locWeight = 0.01;
      Object* hoiCapInfo = findHoiProvInfoFromHoiId((*hoi)->safeGetString("capital"));
      if ((hoiCapInfo) && (hoiCapInfo->safeGetString("continent") == hInf->safeGetString("continent"))) locWeight *= 3;
      Object* vicState = vicProvinceToVicStateMap[vLoc];
      if ((vicState) && (vicState->safeGetString("is_colonial", "no") == "yes")) locWeight *= 0.1;
      /*if (find(forbid.begin(), forbid.end(), hLoc->safeGetString("id")) != forbid.end()) {
	locWeight = 0;
	Logger::logStream(DebugUnits) << "Ignoring location " << hLoc->safeGetString("id") << "\n"; 
	}*/
      
      (*varm)->resetLeaf("locationWeight", locWeight);
      /*
      Logger::logStream(Logger::Debug) << "Location "
				       << hLoc->safeGetString("id") << " "
				       << (*varm)->safeGetString("name") 
				       << " has weight "
				       << locWeight << "*" << (int) (*varm)->getValue("regiment").size() << " = " 
				       << (*varm)->getValue("regiment").size() * locWeight
				       << "\n";
      */
      totalArmyWeight += (*varm)->getValue("regiment").size() * locWeight;
    }
   
    objvec vNavies = vicCountry->getValue("navy");
    objvec vicNavies;
    for (objiter varm = vNavies.begin(); varm != vNavies.end(); ++varm) {     
      vector<pair<Object*, string> > failures;
      Object* hLoc = findNavyLocation(vicGame->safeGetObject((*varm)->safeGetString("location")), failures, (*hoi));
      if (!hLoc) hLoc = findNavyLocation(vicGame->safeGetObject((*varm)->safeGetString("base")), failures, (*hoi));
      if (!hLoc) {
	Logger::logStream(DebugUnits) << "Not using "
				      << (*hoi)->safeGetString("tag") 
				      << " navy "
				      << (*varm)->safeGetString("name")
				      << " for navy location because no suitable conversion of province "
				      << (*varm)->safeGetString("location") 
				      << " was found. These provinces were tried: \n";
	for (unsigned int i = 0; i < failures.size(); ++i) {
	  Logger::logStream(DebugUnits) << "  "
					<< failures[i].first->safeGetString("id")
					<< " : "
					<< failures[i].second
					<< "\n"; 
	}
	continue;
      }
      (*varm)->resetLeaf("hoiLocation", hLoc->safeGetString("id"));       
      vicNavies.push_back(*varm); 
    }

    if ((0 == vicNavies.size()) && (0 < vNavies.size())) {
      // Make a dummy navy in a location with a naval base
      string location = "none"; 
      for (objiter hprov = hoiProvinces.begin(); hprov != hoiProvinces.end(); ++hprov) {
	if ((*hoi)->safeGetString("tag") != (*hprov)->safeGetString("owner")) continue;
	if ((*hoi)->safeGetString("tag") != (*hprov)->safeGetString("controller")) continue;
	if (!(*hprov)->safeGetObject("naval_base")) continue;

	location = (*hprov)->safeGetString("id");
	break;
      }
      if (location != "none") {
	Object* vicNavy = vNavies[0];
	Object* hoiNavy = new Object("navalunit");
	vicNavy->setValue(hoiNavy);
	vicNavies.push_back(vicNavy);
	vicNavy->resetLeaf("hoiLocation", location); 
	
	Object* hoiId = new Object("id");
	hoiId->setLeaf("type", "14500");
	hoiId->setLeaf("id", numNavies++);
	hoiNavy->setValue(hoiId); 
	hoiNavy->setLeaf("name", vicNavy->safeGetString("name", "Default Fleet Name"));
	hoiNavy->setLeaf("location", vicNavy->safeGetString("hoiLocation"));
	hoiNavy->setLeaf("home", vicNavy->safeGetString("hoiLocation"));
	hoiNavy->setLeaf("base", vicNavy->safeGetString("hoiLocation"));	  
	hoiNavy->setLeaf("development", "no");
	(*hoi)->setValue(hoiNavy);

	Logger::logStream(DebugUnits) << "As backup, placing "
				      << (*hoi)->safeGetString("tag")
				      << " navies in province "
				      << location
				      << ".\n"; 
      }
    }
    
    if (0 == vicArmies.size() + vicNavies.size()) continue; 

    Object* dummyReg = new Object("dummyreg"); 
    int numGenerated = 0;
    objvec hoiDivisions;
    for (map<string, int>::iterator u = landdivs.begin(); u != landdivs.end(); ++u) {
      if (0.01 > totalVicWeights[(*u).first]) continue;
      double curr = (*u).second * unitWeights->safeGetFloat((*u).first) / totalVicWeights[(*u).first];
      Logger::logStream(DebugUnits) << (*u).first << " weight for tag "
				    << (*hoi)->safeGetString("tag") << " is "
				    << curr << " "
				    << (*u).second << " "
				    << unitWeights->safeGetFloat((*u).first) << " "
				    << totalVicWeights[(*u).first] << " "
				    << overflow[(*u).first] 
				    << "\n"; 
      if (overflow[(*u).first] > 0) {
	curr += overflow[(*u).first];
	overflow[(*u).first] = 0;
      }
      int numDivs = (int) floor(curr + 0.5);
      overflow[(*u).first] += (curr - numDivs);

      int model = getModel((*hoi), (*u).first);
      model += bonusLevel[(*u).first];
      if (model < 0) model = 0; 
      
      for (int i = 0; i < numDivs; ++i) {
	Object* vicArmy = vicArmies[i % vicArmies.size()];
	string name = vicArmy->safeGetString("name", "Default Army Name");
	
	double roll = rand() % 10000;
	//roll /= 10000;
	//roll *= totalArmyWeight; 
	double cumWeight = 0;
	for (unsigned int ii = 0; ii < vicArmies.size(); ++ii) {
	  cumWeight += vicArmies[ii]->getValue("regiment").size() * vicArmies[ii]->safeGetFloat("locationWeight");
	  if (cumWeight < (roll/10000)*totalArmyWeight) continue;
	  vicArmy = vicArmies[ii];
	  break; 
	}

	objvec regs = vicArmy->getValue("regiment");
	Object* randReg = regs[rand() % regs.size()];
	if (!randReg) randReg = dummyReg; 
	
	Object* hoiArmy = vicArmy->safeGetObject("landunit");
	if (!hoiArmy) {
	  hoiArmy = new Object("landunit");
	  vicArmy->setValue(hoiArmy);
	  
	  Object* hoiId = new Object("id");
	  hoiId->setLeaf("type", "23500");
	  hoiId->setLeaf("id", numArmies++);
	  hoiArmy->setValue(hoiId); 
	  hoiArmy->setLeaf("name", name); 
	  string vicLocation = vicArmy->safeGetString("location");
	  Object* vLoc = vicGame->safeGetObject(vicLocation);
	  assert(vLoc);
	  Object* hLoc = vicProvinceToHoiProvincesMap[vLoc][0];
	  hoiArmy->setLeaf("location", hLoc->safeGetString("id"));
	  hoiArmy->setLeaf("home", hLoc->safeGetString("id"));
	  hoiArmy->setLeaf("development", "no");
	  (*hoi)->setValue(hoiArmy);
	  /*
	  Logger::logStream(Logger::Debug) << "Using army "
					   << vicArmy->safeGetString("name")
					   << " with location weight "
					   << vicArmy->safeGetFloat("locationWeight", 1) * vicArmy->getValue("regiment").size() 
					   << " at location "
					   << hLoc->safeGetString("id")
					   << " with roll "
					   << roll << ", "
					   << cumWeight << " / "
					   << totalArmyWeight 
					   << "\n";
	  */

	}
	
	Object* division = new Object("division");
	hoiArmy->setValue(division);
	Object* divId = new Object("id");
	divId->setLeaf("type", "12700");
	divId->setLeaf("id", numRegiments++);
	division->setValue(divId);
	
	division->setLeaf("name", vicArmyNames[vicCountry][numGenerated++ % vicArmyNames[vicCountry].size()]);
	division->setLeaf("model", model); 
	division->setLeaf("type", (*u).first);
	division->setLeaf("strength", "100.000");
	division->setLeaf("max_oil_stock", unitTypeToMaxSuppliesMap[(*u).first]["none"].first);
	division->setLeaf("oil", unitTypeToMaxSuppliesMap[(*u).first]["none"].first);
	division->setLeaf("max_supply_stock", unitTypeToMaxSuppliesMap[(*u).first]["none"].second);
	division->setLeaf("supplies", unitTypeToMaxSuppliesMap[(*u).first]["none"].second);
	division->setLeaf("experience", randReg->safeGetString("experience")); 
	hoiDivisions.push_back(division); 
      }
    }
    
    for (map<string, int>::iterator b = landbrigs.begin(); b != landbrigs.end(); ++b) {
      if (0.01 > totalVicWeights[(*b).first]) continue;
      double curr = (*b).second * unitWeights->safeGetFloat((*b).first) / totalVicWeights[(*b).first];
      if (overflow[(*b).first] > 0) {
	curr += overflow[(*b).first];
	overflow[(*b).first] = 0;
      }
      int numBrigs = (int) floor(curr + 0.5);
      overflow[(*b).first] += (curr - numBrigs);

      int model = getModel((*hoi), (*b).first);
      model += bonusLevel[(*b).first];
      if (model < 0) model = 0; 
      
      for (int i = 0; i < numBrigs; ++i) {
	Object* divToAttach = 0;

	for (unsigned int i = 0; i < hoiDivisions.size(); ++i) {
	  if (!brigadeAcceptable[hoiDivisions[i]->safeGetString("type")][(*b).first]) continue;
	  divToAttach = hoiDivisions[i];
	  hoiDivisions[i] = hoiDivisions.back();
	  hoiDivisions.pop_back();
	  break; 
	}
	if (divToAttach) {
	  divToAttach->setLeaf("extra", (*b).first);
	  divToAttach->setLeaf("brigade_model", model);
	  string unittype = divToAttach->safeGetString("type");
	  divToAttach->resetLeaf("max_oil_stock", unitTypeToMaxSuppliesMap[unittype][(*b).first].first);
	  divToAttach->resetLeaf("oil", unitTypeToMaxSuppliesMap[unittype][(*b).first].first);
	  divToAttach->resetLeaf("max_supply_stock", unitTypeToMaxSuppliesMap[unittype][(*b).first].second);
	  divToAttach->resetLeaf("supplies", unitTypeToMaxSuppliesMap[unittype][(*b).first].second);			  
	  continue;
	}
	Object* queuedBrigade = new Object("landdivision");
	(*hoi)->setValue(queuedBrigade); 
	Object* divId = new Object("id");
	divId->setLeaf("type", "4712");
	divId->setLeaf("id", forty712s++);
	queuedBrigade->setValue(divId);
	
	queuedBrigade->setLeaf("name", vicArmyNames[vicCountry][numGenerated++ % vicArmyNames[vicCountry].size()]);
	queuedBrigade->setLeaf("model", model); 
	queuedBrigade->setLeaf("brigade_model", model); 
	queuedBrigade->setLeaf("extra", (*b).first);
	queuedBrigade->setLeaf("strength", "100.000");
      }
    }

    // Air units 
    hoiDivisions.clear();
    Object* hoiAirUnit = new Object("airunit");
    objvec hoiAirUnits;
    hoiAirUnits.push_back(hoiAirUnit);
    for (map<string, int>::iterator u = airdivs.begin(); u != airdivs.end(); ++u) {
      if (0.01 > totalVicWeights[(*u).first]) continue;
      double curr = (*u).second * unitWeights->safeGetFloat((*u).first) / totalVicWeights[(*u).first];
      if (overflow[(*u).first] > 0) {
	curr += overflow[(*u).first];
	overflow[(*u).first] = 0;
      }
      int numDivs = (int) floor(curr + 0.5);
      overflow[(*u).first] += (curr - numDivs);

      int model = getModel((*hoi), (*u).first);
      model += bonusLevel[(*u).first];
      if (model < 0) model = 0; 
      
      for (int i = 0; i < numDivs; ++i) {
	Object* division = new Object("division");
	hoiAirUnit->setValue(division);
	if (hoiAirUnit->getValue("division").size() > 3) {
	  hoiAirUnit = new Object("airunit");
	  hoiAirUnits.push_back(hoiAirUnit);
	}
	Object* divId = new Object("id");
	divId->setLeaf("type", "13500");
	divId->setLeaf("id", numRegiments++);
	division->setValue(divId);

	Object* vicArmy = vicArmies[i % vicArmies.size()];
	double roll = rand() % 10000;
	double cumWeight = 0;
	for (unsigned int ii = 0; ii < vicArmies.size(); ++ii) {
	  cumWeight += vicArmies[ii]->getValue("regiment").size() * vicArmies[ii]->safeGetFloat("locationWeight");
	  if (cumWeight < (roll/10000)*totalArmyWeight) continue;
	  vicArmy = vicArmies[ii];
	  break; 
	}

	objvec regs = vicArmy->getValue("regiment");
	Object* randReg = regs[rand() % regs.size()];
	if (!randReg) randReg = dummyReg; 
	
	division->setLeaf("name", vicArmyNames[vicCountry][numGenerated++ % vicArmyNames[vicCountry].size()]);
	division->setLeaf("model", model); 
	division->setLeaf("type", (*u).first);
	division->setLeaf("strength", "100.000");
	division->setLeaf("max_oil_stock", unitTypeToMaxSuppliesMap[(*u).first]["none"].first);
	division->setLeaf("oil", unitTypeToMaxSuppliesMap[(*u).first]["none"].first);
	division->setLeaf("max_supply_stock", unitTypeToMaxSuppliesMap[(*u).first]["none"].second);
	division->setLeaf("supplies", unitTypeToMaxSuppliesMap[(*u).first]["none"].second);
	division->setLeaf("experience", randReg->safeGetString("experience")); 
	hoiDivisions.push_back(division); 
      }
    }
    if (0 < hoiDivisions.size()) {
      for (objiter hau = hoiAirUnits.begin(); hau != hoiAirUnits.end(); ++hau) {
	(*hoi)->setValue(*hau);
	Object* airID = new Object("id");
	airID->setLeaf("type", "10848");
	airID->setLeaf("id", numAirUnits++);
	(*hau)->setValue(airID); 
	(*hau)->setLeaf("name", "\"Air Force\"");
	string cap = (*hoi)->safeGetString("capital");
	(*hau)->setLeaf("location", cap);
	(*hau)->setLeaf("base", cap);
	(*hau)->setLeaf("home", cap);
	Object* capProv = findHoiProvinceFromHoiId(cap); 
	if (!capProv->safeGetObject("air_base")) {
	  Object* airbase = new Object("air_base");
	  airbase->setLeaf("type", "air_base");
	  airbase->setLeaf("location", cap);
	  airbase->setLeaf("size", "1.000");
	  airbase->setLeaf("current_size", "1.000");
	  capProv->setValue(airbase); 	
	}
      }
    }


    for (map<string, int>::iterator b = airbrigs.begin(); b != airbrigs.end(); ++b) {
      if (0.01 > totalVicWeights[(*b).first]) continue;
      double curr = (*b).second * unitWeights->safeGetFloat((*b).first) / totalVicWeights[(*b).first];
      if (overflow[(*b).first] > 0) {
	curr += overflow[(*b).first];
	overflow[(*b).first] = 0;
      }
      int numBrigs = (int) floor(curr + 0.5);
      overflow[(*b).first] += (curr - numBrigs);

      int model = getModel((*hoi), (*b).first);
      model += bonusLevel[(*b).first];
      if (model < 0) model = 0; 

      for (int i = 0; i < numBrigs; ++i) {
	Object* divToAttach = 0;
	for (unsigned int i = 0; i < hoiDivisions.size(); ++i) {
	  if (!brigadeAcceptable[hoiDivisions[i]->safeGetString("type")][(*b).first]) continue;
	  divToAttach = hoiDivisions[i];
	  hoiDivisions[i] = hoiDivisions.back();
	  hoiDivisions.pop_back();
	  break; 
	}
	if (divToAttach) {
	  divToAttach->setLeaf("extra", (*b).first);
	  divToAttach->setLeaf("brigade_model", model);
	  string unittype = divToAttach->safeGetString("type");	  
	  divToAttach->resetLeaf("max_oil_stock", unitTypeToMaxSuppliesMap[unittype][(*b).first].first);
	  divToAttach->resetLeaf("oil", unitTypeToMaxSuppliesMap[unittype][(*b).first].first);
	  divToAttach->resetLeaf("max_supply_stock", unitTypeToMaxSuppliesMap[unittype][(*b).first].second);
	  divToAttach->resetLeaf("supplies", unitTypeToMaxSuppliesMap[unittype][(*b).first].second);			  	  
	  continue;
	}
	Object* queuedBrigade = new Object("airdivision");
	(*hoi)->setValue(queuedBrigade); 
	Object* divId = new Object("id");
	divId->setLeaf("type", "4712");
	divId->setLeaf("id", forty712s++);
	queuedBrigade->setValue(divId);
	
	queuedBrigade->setLeaf("name", vicArmyNames[vicCountry][numGenerated++ % vicArmyNames[vicCountry].size()]);
	queuedBrigade->setLeaf("model", model); 
	queuedBrigade->setLeaf("brigade_model", model); 
	queuedBrigade->setLeaf("extra", (*b).first);
	queuedBrigade->setLeaf("strength", "100.000");
      }
    }

    
    // Navies
    hoiDivisions.clear(); 
    for (map<string, int>::iterator u = navaldivs.begin(); u != navaldivs.end(); ++u) {
      if (0.01 > totalVicWeights[(*u).first]) continue;
      if (0 == vicNavies.size()) { 
	Logger::logStream(Logger::Warning) << "Warning: Could not find good navy location for tag "
					   << (*hoi)->safeGetString("tag")
					   << ", ignoring warships.\n";
	break; 
      }
      double curr = (*u).second * unitWeights->safeGetFloat((*u).first) / totalVicWeights[(*u).first];
      if (overflow[(*u).first] > 0) {
	curr += overflow[(*u).first];
	overflow[(*u).first] = 0;
      }
      int numDivs = (int) floor(curr + 0.5);
      overflow[(*u).first] += (curr - numDivs);

      int model = getModel((*hoi), (*u).first);
      model += bonusLevel[(*u).first];
      if (model < 0) model = 0; 
      
      for (int i = 0; i < numDivs; ++i) {
	Object* vicNavy = vicNavies[i % vicNavies.size()];
	Object* hoiNavy = vicNavy->safeGetObject("navalunit");
	if (!hoiNavy) {
	  hoiNavy = new Object("navalunit");
	  vicNavy->setValue(hoiNavy);
	
	  Object* hoiId = new Object("id");
	  hoiId->setLeaf("type", "14500");
	  hoiId->setLeaf("id", numNavies++);
	  hoiNavy->setValue(hoiId); 
	  hoiNavy->setLeaf("name", vicNavy->safeGetString("name", "Default Fleet Name"));
	  hoiNavy->setLeaf("location", vicNavy->safeGetString("hoiLocation"));
	  hoiNavy->setLeaf("home", vicNavy->safeGetString("hoiLocation"));
	  hoiNavy->setLeaf("base", vicNavy->safeGetString("hoiLocation"));	  
	  hoiNavy->setLeaf("development", "no");
	  (*hoi)->setValue(hoiNavy);
	}

	objvec regs = vicNavy->getValue("ship");
	Object* randReg = regs[rand() % regs.size()];
	if (!randReg) randReg = dummyReg; 
	
	Object* division = new Object("division");
	hoiNavy->setValue(division);
	Object* divId = new Object("id");
	divId->setLeaf("type", "14500");
	divId->setLeaf("id", numNavies++); 
	division->setValue(divId);
	
	division->setLeaf("name", vicNavyNames[vicCountry][numGenerated++ % vicNavyNames[vicCountry].size()]);
	division->setLeaf("model", model); 
	division->setLeaf("type", (*u).first);
	division->setLeaf("strength", "100.000");
	division->setLeaf("max_oil_stock", unitTypeToMaxSuppliesMap[(*u).first]["none"].first);
	division->setLeaf("oil", unitTypeToMaxSuppliesMap[(*u).first]["none"].first);
	division->setLeaf("max_supply_stock", unitTypeToMaxSuppliesMap[(*u).first]["none"].second);
	division->setLeaf("experience", randReg->safeGetString("experience")); 	
	division->setLeaf("supplies", unitTypeToMaxSuppliesMap[(*u).first]["none"].second);			
	hoiDivisions.push_back(division); 
      }
    }

    for (map<string, int>::iterator b = navalbrigs.begin(); b != navalbrigs.end(); ++b) {
      if (0.01 > totalVicWeights[(*b).first]) continue;      
      double curr = (*b).second * unitWeights->safeGetFloat((*b).first) / totalVicWeights[(*b).first];
      if (overflow[(*b).first] > 0) {
	curr += overflow[(*b).first];
	overflow[(*b).first] = 0;
      }
      int numBrigs = (int) floor(curr + 0.5);
      overflow[(*b).first] += (curr - numBrigs);

      int model = getModel((*hoi), (*b).first);
      model += bonusLevel[(*b).first];
      if (model < 0) model = 0; 
      
      for (int i = 0; i < numBrigs; ++i) {
	Object* divToAttach = 0;
	for (unsigned int i = 0; i < hoiDivisions.size(); ++i) {
	  if (!brigadeAcceptable[hoiDivisions[i]->safeGetString("type")][(*b).first]) continue;
	  divToAttach = hoiDivisions[i];
	  hoiDivisions[i] = hoiDivisions.back();
	  hoiDivisions.pop_back();
	  break; 
	}
	if (divToAttach) {
	  divToAttach->setLeaf("extra1", (*b).first);
	  divToAttach->setLeaf("brigade_model", model);
	  string unittype = divToAttach->safeGetString("type");	  
	  divToAttach->resetLeaf("max_oil_stock", unitTypeToMaxSuppliesMap[unittype][(*b).first].first);
	  divToAttach->resetLeaf("oil", unitTypeToMaxSuppliesMap[unittype][(*b).first].first);
	  divToAttach->resetLeaf("max_supply_stock", unitTypeToMaxSuppliesMap[unittype][(*b).first].second);
	  divToAttach->resetLeaf("supplies", unitTypeToMaxSuppliesMap[unittype][(*b).first].second);			  	  
	  continue;
	}
	Object* queuedBrigade = new Object("navaldivision");
	(*hoi)->setValue(queuedBrigade); 
	Object* divId = new Object("id");
	divId->setLeaf("type", "4712");
	divId->setLeaf("id", forty712s++);
	queuedBrigade->setValue(divId);
	
	queuedBrigade->setLeaf("name", vicNavyNames[vicCountry][numGenerated++ % vicNavyNames[vicCountry].size()]);
	queuedBrigade->setLeaf("model", model); 
	queuedBrigade->setLeaf("brigade_model", getModel((*hoi), (*b).first));	
	queuedBrigade->setLeaf("extra", (*b).first);
	queuedBrigade->setLeaf("strength", "100.000");
      }
    }
  }
}

void WorkerThread::convert () {
  if (!vicGame) {
    Logger::logStream(Logger::Game) << "No file loaded.\n";
    return; 
  }
  
  Logger::logStream(Logger::Game) << "Loading HoI source file.\n";
  hoiGame = processFile(targetVersion + "input.eug");

  objvec dprovs = configObject->getValue("debugProvince");
  for (objiter dp = dprovs.begin(); dp != dprovs.end(); ++dp) {
    string tag = (*dp)->getLeaf();
    Object* prov = vicGame->safeGetObject(tag);
    if (!prov) continue; 
    prov->setLeaf("debug", "yes");
    objvec leaves = prov->getLeaves();
    for (objiter l = leaves.begin(); l != leaves.end(); ++l) {
      if (configObject->safeGetString((*l)->safeGetString("id")) != "debugPop") continue;
      (*l)->resetLeaf("debugPop", "yes");
      Logger::logStream(Logger::Debug) << "Set debug pop " << (*l)->safeGetString("id") << "\n"; 
    }
  }

  fillVicVectors(); 
  
  createProvinceMappings();
  createCountryMappings();
  createUtilityMaps();
  prepareCountries();
  moveProvinces();
  setAcceptedStatus(); 
  moveControls();
  moveCores(); 
  moveResources();
  moveCapitals();   
  calculateKustomPoints();
  calculateCountryQualities(); // Must occur after Kustom so voters are initialised, but before sliders 
  sliders(); 
  moveTechTeams();
  moveLeaders();
  calculateGovTypes();
  moveMinisters();  
  diplomacy();
  dissent(); 
  fixHeader();
  fixGlobals();
  techs(); // Must occur after moveMinisters so cabinet is set
  provinceStructures(); 
  units();
  stockpiles(); // Must occur after units to set supported regiments.
  mobEvents(); // Must be after stockpiles to set mobManpower 
  ideas();
  revolters();
  desperationMoveCapitals(); 
  cleanUp(); 
  
  
  Logger::logStream(Logger::Game) << "Done with conversion, writing to file.\n"; 
  ofstream writer;
  writer.open(".\\Output\\converted.eug");
  Parser::topLevel = hoiGame;
  writer << (*hoiGame);
  writer.close();
  Logger::logStream(Logger::Game) << "Done writing.\n";  
}

void VoterInfo::addPop (Object* pop, Object* voterConfig) {
  int weight = 1;
  if (voterConfig) weight = voterConfig->safeGetInt(pop->getKey(), 0);
  if ((voterConfig->safeGetString("acceptedOnly", "yes") == "yes") && (pop->safeGetString("acceptedCulture", "no") == "no")) weight = 0; 
  
  double size = pop->safeGetFloat("size");
  total += size;
  weightedTotal += size*weight; 
  dissent += size * pop->safeGetFloat("mil"); 
  
  Object* issue = pop->safeGetObject("issues");
  if (issue) { 
    objvec numbers = issue->getLeaves();
    for (objiter n = numbers.begin(); n != numbers.end(); ++n) {
      int idx = atoi((*n)->getKey().c_str());
      if (0 > idx) continue; 
      if (idx >= (int) issues.size()) issues.resize(idx+1);
      issues[idx] += 0.01*size*weight*atof((*n)->getLeaf().c_str()); // Stored as percent, mult by 0.01 to get fractions 
    }
  }

  issue = pop->safeGetObject("ideology");
  if (issue) {
    objvec ideas = issue->getLeaves();
    for (objiter n = ideas.begin(); n != ideas.end(); ++n) {
      int idx = atoi((*n)->getKey().c_str());
      if (0 > idx) continue; 
      if (idx >= (int) ideologies.size()) ideologies.resize(idx+1);
      ideologies[idx] += 0.01*size*weight*atof((*n)->getLeaf().c_str()); 
    }
  }

  pair<string, string> cultrel = extractCulture(pop);
  cultures[cultrel.first]  += size;
  religion[cultrel.second] += size; 
}

double VoterInfo::calculateDiversity () const {
  int numCultures = 0;
  double sizeOfLargest = 0;
  for (map<string, double>::const_iterator i = cultures.begin(); i != cultures.end(); ++i) {
    double percent = (*i).second;
    percent /= total;
    if (percent < 0.005) continue;
    numCultures++;
    if (percent > sizeOfLargest) sizeOfLargest = percent; 
  }

  double ret = numCultures / sizeOfLargest;

  numCultures = 0;
  sizeOfLargest = 0;
  for (map<string, double>::const_iterator i = religion.begin(); i != religion.end(); ++i) {
    double percent = (*i).second;
    percent /= total;
    if (percent < 0.005) continue;
    numCultures++;
    if (percent > sizeOfLargest) sizeOfLargest = percent; 
  }
  ret += numCultures / sizeOfLargest;
  return ret; 
}
