#include <iostream>
#include <cstdio>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
#include <QApplication>
#include <QtUiTools>
#include <QtDebug>

#include "gui/med-qt.hpp"
#include "gui/TreeItem.hpp"
#include "gui/TreeModel.hpp"
#include "gui/StoreTreeModel.hpp"
#include "gui/ComboBoxDelegate.hpp"
#include "gui/CheckBoxDelegate.hpp"
#include "gui/MemEditor.hpp"
#include "gui/CommonEventListener.hpp"
#include "gui/EncodingManager.hpp"
#include "med/med.hpp"

using namespace std;

MainUi::MainUi(QApplication* app) {
  this->app = app;
  loadUiFiles();
  loadProcessUi();
  loadMemEditor();
  setupStatusBar();
  setupScanTreeView();
  setupStoreTreeView();
  setupSignals();
  setupUi();

  scanState = UiState::Idle;
  storeState = UiState::Idle;

  encodingManager = new EncodingManager(this);
}

void MainUi::refresh(MainUi* mainUi) {
  while(1) {
    mainUi->refreshScanTreeView();
    mainUi->refreshStoreTreeView();
    std::this_thread::sleep_for(chrono::milliseconds(REFRESH_RATE));
  }
}

void MainUi::refreshScanTreeView() {
  scanUpdateMutex.lock();
  scanModel->refreshValues();
  scanUpdateMutex.unlock();
}

void MainUi::refreshStoreTreeView() {
  storeUpdateMutex.lock();
  storeModel->refreshValues();
  storeUpdateMutex.unlock();
}

void MainUi::onProcessClicked() {
  med.listProcesses();

  processDialog->show();

  processTreeWidget->clear(); //Remove all items

  //Add all the process into the tree widget
  for(int i=med.processes.size()-1;i>=0;i--) {
    QTreeWidgetItem* item = new QTreeWidgetItem(processTreeWidget);
    item->setText(0, med.processes[i].pid.c_str());
    item->setText(1, med.processes[i].cmdline.c_str());
  }
}

void MainUi::onProcessItemDblClicked(QTreeWidgetItem* item, int) {
  clearAll();

  int index = item->treeWidget()->indexOfTopLevelItem(item); //Get the current row index
  med.selectedProcess = med.processes[med.processes.size() -1 - index];

  //Make changes to the selectedProcess and hide the window
  selectedProcessLine->setText(QString::fromLatin1((med.selectedProcess.pid + " " + med.selectedProcess.cmdline).c_str())); //Do not use fromStdString(), it will append with some unknown characters

  processDialog->hide();
}

void MainUi::onScanClicked() {
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }

  string scanValue = mainWindow->findChild<QLineEdit*>("scanEntry")->text().toStdString();
  if (QString(scanValue.c_str()).trimmed() == "") {
    return;
  }

  string scanType = scanTypeCombo->currentText().toStdString();
  if (scanType == SCAN_TYPE_STRING) {
    scanValue = encodingManager->encode(scanValue);
  }

  scanUpdateMutex.lock();

  try {
    med.scan(scanValue, scanType);
  } catch(MedException &ex) {
    cerr << "scan: "<< ex.what() <<endl;
  }

  scanModel->clearAll();
  if(med.scanAddresses.size() <= SCAN_ADDRESS_VISIBLE_SIZE) {
    scanModel->addScan(scanType);
  }

  updateNumberOfAddresses();
  scanUpdateMutex.unlock();
}

void MainUi::onFilterClicked() {
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }

  string scanValue = mainWindow->findChild<QLineEdit*>("scanEntry")->text().toStdString();
  if (QString(scanValue.c_str()).trimmed() == "") {
    return;
  }

  string scanType = scanTypeCombo->currentText().toStdString();
  if (scanType == SCAN_TYPE_STRING) {
    scanValue = encodingManager->encode(scanValue);
  }

  scanUpdateMutex.lock();

  med.filter(scanValue, scanType);

  if(med.scanAddresses.size() <= SCAN_ADDRESS_VISIBLE_SIZE) {
    scanModel->addScan(scanType);
  }

  updateNumberOfAddresses();
  scanUpdateMutex.unlock();
}

void MainUi::onScanClearClicked() {
  scanUpdateMutex.lock();
  scanModel->empty();
  statusBar->showMessage("Scan cleared");
  scanUpdateMutex.unlock();
}
void MainUi::onStoreClearClicked() {
  storeUpdateMutex.lock();
  storeModel->empty();
  storeUpdateMutex.unlock();
}

void MainUi::onScanAddClicked() {
  auto indexes = scanTreeView
    ->selectionModel()
    ->selectedRows(SCAN_COL_ADDRESS);

  scanUpdateMutex.lock();
  for (int i=0;i<indexes.size();i++) {
    med.addToStoreByIndex(indexes[i].row());
  }

  storeModel->refresh();
  scanUpdateMutex.unlock();
}

void MainUi::onScanAddAllClicked() {
  scanUpdateMutex.lock();
  for(auto i=0;i<med.scanAddresses.size();i++)
    med.addToStoreByIndex(i);
  storeModel->refresh();
  scanUpdateMutex.unlock();
}

void MainUi::onStoreNextClicked() {
  auto indexes = storeTreeView->selectionModel()->selectedRows(STORE_COL_ADDRESS);
  if (indexes.size() == 0) {
    cerr << "onStoreNextClicked: nothing selected" << endl;
    return;
  }

  storeUpdateMutex.lock();
  med.addNextAddress(indexes[0].row());
  storeModel->addRow();
  storeUpdateMutex.unlock();
}

void MainUi::onStorePrevClicked() {
  auto indexes = storeTreeView->selectionModel()->selectedRows(STORE_COL_ADDRESS);
  if (indexes.size() == 0) {
    cerr << "onStorePrevClicked: nothing selected" << endl;
    return;
  }

  storeUpdateMutex.lock();
  med.addPrevAddress(indexes[0].row());
  storeModel->addRow();
  storeUpdateMutex.unlock();
}


void storeShift(MainUi* mainUi, bool reverse = false) {
  auto mainWindow = mainUi->mainWindow;
  auto &med = mainUi->med;
  auto storeTreeView = mainUi->storeTreeView;
  auto &storeUpdateMutex = mainUi->storeUpdateMutex;
  auto storeModel = mainUi->storeModel;
  auto statusBar = mainUi->statusBar;

  long shiftFrom, shiftTo, difference;
  try {
    shiftFrom = hexToInt(mainWindow->findChild<QLineEdit*>("shiftFrom")->text().toStdString());
    shiftTo = hexToInt(mainWindow->findChild<QLineEdit*>("shiftTo")->text().toStdString());
    if (reverse) {
      difference = shiftFrom - shiftTo;
    }
    else {
      difference = shiftTo - shiftFrom;
    }
  } catch(MedException &e) {
    cerr << e.what() << endl;
  }

  //Get PID
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }

  auto indexes = storeTreeView
    ->selectionModel()
    ->selectedRows(STORE_COL_ADDRESS);

  storeUpdateMutex.lock();
  for (auto i=0;i<indexes.size();i++) {
    med.shiftStoreAddressByIndex(indexes[i].row(), difference);
  }
  storeModel->refresh();
  storeUpdateMutex.unlock();
}

void MainUi::onStoreShiftClicked() {
  storeShift(this);
}

void MainUi::onStoreUnshiftClicked() {
  storeShift(this, true);
}

void MainUi::onStoreMoveClicked() {
  long moveSteps;
  try {
    moveSteps = mainWindow->findChild<QLineEdit*>("shiftTo")->text().toInt();
  } catch(MedException &e) {
    cerr << e.what() << endl;
  }

  //Get PID
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }

  auto indexes = storeTreeView
    ->selectionModel()
    ->selectedRows(STORE_COL_ADDRESS);

  storeUpdateMutex.lock();
  for (auto i=0;i<indexes.size();i++) {
    med.shiftStoreAddressByIndex(indexes[i].row(), moveSteps);
  }
  storeModel->refresh();
  storeUpdateMutex.unlock();
}


///////////////////
// Menu items
///////////////////

void MainUi::onSaveAsTriggered() {
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }
  QString filename = QFileDialog::getSaveFileName(mainWindow,
                                                  QString("Save JSON"),
                                                  "./",
                                                  QString("Save JSON (*.json)"));

  if (filename == "") {
    return;
  }
  this->filename = filename;
  setWindowTitle();

  med.saveFile(filename.toStdString().c_str());
}

void MainUi::onOpenTriggered() {
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }
  QString filename = QFileDialog::getOpenFileName(mainWindow,
                                                  QString("Open JSON"),
                                                  "./",
                                                  QString("Open JSON (*.json)"));
  if (filename == "") {
    return;
  }

  openFile(filename);
}

void MainUi::onReloadTriggered() {
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }
  if (filename == "") {
    return;
  }

  openFile(filename);
}

void MainUi::openFile(QString filename) {
  this->filename = filename;
  setWindowTitle();

  med.openFile(filename.toStdString().c_str());

  storeUpdateMutex.lock();
  storeModel->clearAll();
  storeModel->refresh();
  storeUpdateMutex.unlock();

  notesArea->setPlainText(QString::fromStdString(med.notes));
}


void MainUi::onNewAddressTriggered() {
  if(med.selectedProcess.pid == "") {
    statusBar->showMessage("No process selected");
    return;
  }
  storeUpdateMutex.lock();
  med.addNewAddress();
  storeModel->addRow();
  storeUpdateMutex.unlock();
}

void MainUi::onDeleteAddressTriggered() {
  auto indexes = storeTreeView
    ->selectionModel()
    ->selectedRows(STORE_COL_ADDRESS);

  //Sort and reverse
  sort(indexes.begin(), indexes.end(), [](QModelIndex a, QModelIndex b) {
      return a.row() > b.row();
    });

  storeUpdateMutex.lock();
  for (auto i=0;i<indexes.size();i++) {
    med.deleteAddressByIndex(indexes[i].row());
  }
  storeModel->refresh();
  storeUpdateMutex.unlock();
}

void MainUi::onMemEditorTriggered() {
  memEditor->show();
}


/////////////////////////////
// Scan and Store tree view
/////////////////////////////

void MainUi::onScanTreeViewClicked(const QModelIndex &index) {
  if(index.column() == SCAN_COL_TYPE) {
    scanTreeView->edit(index); //Trigger edit by 1 click
  }
}

void MainUi::onScanTreeViewDoubleClicked(const QModelIndex &index) {
  if (index.column() == SCAN_COL_VALUE) {
    scanUpdateMutex.lock();
    scanState = UiState::Editing;
  }
}

void MainUi::onStoreTreeViewDoubleClicked(const QModelIndex &index) {
  if (index.column() == STORE_COL_VALUE) {
    storeUpdateMutex.lock();
    storeState = UiState::Editing;
  }
}

void MainUi::onStoreTreeViewClicked(const QModelIndex &index) {
  if (index.column() == STORE_COL_TYPE) {
    storeTreeView->edit(index);
  }
  else if (index.column() == STORE_COL_LOCK) {
    storeTreeView->edit(index);
  }
}

void MainUi::onScanTreeViewDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // qDebug() << topLeft << bottomRight << roles;
  if (topLeft.column() == SCAN_COL_VALUE) {
    tryUnlock(scanUpdateMutex);
    scanState = UiState::Idle;
  }
}

void MainUi::onStoreTreeViewDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
  // qDebug() << topLeft << bottomRight << roles;
  if (topLeft.column() == STORE_COL_VALUE) {
    tryUnlock(storeUpdateMutex);
    storeState = UiState::Idle;
  }
}

void MainUi::onStoreHeaderClicked(int logicalIndex) {
  if (logicalIndex == STORE_COL_DESCRIPTION) {
    storeModel->sortByDescription();
  }
  else if (logicalIndex == STORE_COL_ADDRESS) {
    storeModel->sortByAddress();
  }
}

void MainUi::loadUiFiles() {
  QUiLoader loader;
  QFile file("./main-qt.ui");
  file.open(QFile::ReadOnly);
  mainWindow = loader.load(&file);
  file.close();

  scanTreeView = mainWindow->findChild<QTreeView*>("scanTreeView");
  storeTreeView = mainWindow->findChild<QTreeView*>("storeTreeView");
  selectedProcessLine = mainWindow->findChild<QLineEdit*>("selectedProcess");
  scanTypeCombo = mainWindow->findChild<QComboBox*>("scanType");
  notesArea = mainWindow->findChild<QPlainTextEdit*>("notes");

  scanTreeView->installEventFilter(new ScanTreeEventListener(scanTreeView, this));
  storeTreeView->installEventFilter(new StoreTreeEventListener(storeTreeView, this));
}

void MainUi::loadProcessUi() {
  QUiLoader loader;
  //Cannot put the followings to another method
  processDialog = new QDialog(mainWindow); //If put this to another method, then I cannot set the mainWindow as the parent
  QFile processFile("./process.ui");
  processFile.open(QFile::ReadOnly);

  processSelector = loader.load(&processFile, processDialog);
  processFile.close();

  processTreeWidget = processSelector->findChild<QTreeWidget*>("processTreeWidget");

  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(processSelector);
  processDialog->setLayout(layout);
  processDialog->setModal(true);
  processDialog->resize(400, 400);

  //Add signal
  QObject::connect(processTreeWidget, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this, SLOT(onProcessItemDblClicked(QTreeWidgetItem*, int)));

  processTreeWidget->installEventFilter(new ProcessDialogEventListener(this));
}

void MainUi::loadMemEditor() {
  memEditor = new MemEditor(this);
}

void MainUi::setupStatusBar() {
  //Statusbar message
  statusBar = mainWindow->findChild<QStatusBar*>("statusbar");
  statusBar->showMessage("Tips: Left panel is scanned address. Right panel is stored address.");
}

void MainUi::setupScanTreeView() {
  scanModel = new TreeModel(&med, mainWindow);
  scanTreeView->setModel(scanModel);
  ComboBoxDelegate* delegate = new ComboBoxDelegate();
  scanTreeView->setItemDelegateForColumn(SCAN_COL_TYPE, delegate);
  QObject::connect(scanTreeView,
                   SIGNAL(clicked(QModelIndex)),
                   this,
                   SLOT(onScanTreeViewClicked(QModelIndex)));
  QObject::connect(scanTreeView,
                   SIGNAL(doubleClicked(QModelIndex)),
                   this,
                   SLOT(onScanTreeViewDoubleClicked(QModelIndex)));

  QObject::connect(scanModel,
                   SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)),
                   this,
                   SLOT(onScanTreeViewDataChanged(QModelIndex, QModelIndex, QVector<int>)));
  scanTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void MainUi::setupStoreTreeView() {
  storeModel = new StoreTreeModel(&med, mainWindow);
  storeTreeView->setModel(storeModel);
  ComboBoxDelegate* storeDelegate = new ComboBoxDelegate();
  storeTreeView->setItemDelegateForColumn(STORE_COL_TYPE, storeDelegate);
  CheckBoxDelegate* storeLockDelegate = new CheckBoxDelegate();
  storeTreeView->setItemDelegateForColumn(STORE_COL_LOCK, storeLockDelegate);
  QObject::connect(storeTreeView,
                   SIGNAL(clicked(QModelIndex)),
                   this,
                   SLOT(onStoreTreeViewClicked(QModelIndex)));
  QObject::connect(storeTreeView,
                   SIGNAL(doubleClicked(QModelIndex)),
                   this,
                   SLOT(onStoreTreeViewDoubleClicked(QModelIndex)));

  QObject::connect(storeModel,
                   SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)),
                   this,
                   SLOT(onStoreTreeViewDataChanged(QModelIndex, QModelIndex, QVector<int>)));

  auto* header = storeTreeView->header();
  header->setSectionsClickable(true);
  QObject::connect(header,
                   SIGNAL(sectionClicked(int)),
                   this,
                   SLOT(onStoreHeaderClicked(int)));

  storeTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void MainUi::setupSignals() {
  //Add signal to the process
  QObject::connect(mainWindow->findChild<QWidget*>("process"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onProcessClicked()));

  //Add signal to scan
  QObject::connect(mainWindow->findChild<QWidget*>("scanButton"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanClicked()));

  QObject::connect(mainWindow->findChild<QWidget*>("filterButton"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onFilterClicked()));

  QObject::connect(mainWindow->findChild<QWidget*>("scanClear"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanClearClicked())
                   );

  QObject::connect(mainWindow->findChild<QPushButton*>("scanAddAll"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanAddAllClicked())
                   );

  QObject::connect(mainWindow->findChild<QPushButton*>("scanAdd"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onScanAddClicked())
                   );

  QObject::connect(mainWindow->findChild<QPushButton*>("nextAddress"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreNextClicked()));
  QObject::connect(mainWindow->findChild<QPushButton*>("prevAddress"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStorePrevClicked()));

  QObject::connect(mainWindow->findChild<QPushButton*>("storeClear"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreClearClicked()));

  QObject::connect(mainWindow->findChild<QPushButton*>("storeShift"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreShiftClicked()));
  QObject::connect(mainWindow->findChild<QPushButton*>("storeUnshift"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreUnshiftClicked()));
  QObject::connect(mainWindow->findChild<QPushButton*>("moveAddress"),
                   SIGNAL(clicked()),
                   this,
                   SLOT(onStoreMoveClicked()));

  QObject::connect(mainWindow->findChild<QAction*>("actionSaveAs"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onSaveAsTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionOpen"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onOpenTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionSave"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onSaveTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionQuit"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onQuitTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionReload"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onReloadTriggered()));

  QObject::connect(mainWindow->findChild<QAction*>("actionNewAddress"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onNewAddressTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionDeleteAddress"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onDeleteAddressTriggered()));
  QObject::connect(mainWindow->findChild<QAction*>("actionMemEditor"),
                   SIGNAL(triggered()),
                   this,
                   SLOT(onMemEditorTriggered()));

  QObject::connect(mainWindow->findChild<QAction*>("actionShowNotes"),
                   SIGNAL(triggered(bool)),
                   this,
                   SLOT(onShowNotesTriggered(bool)));
  QObject::connect(notesArea,
                   SIGNAL(textChanged()),
                   this,
                   SLOT(onNotesAreaChanged()));
}

void MainUi::setupUi() {
  //Set default scan type
  scanTypeCombo->setCurrentIndex(1);

  //TODO: center
  mainWindow->show();

  qRegisterMetaType<QVector<int>>(); //For multithreading.

  //Multi-threading
  refreshThread = new std::thread(MainUi::refresh, this);

  QAction* showNotesAction = mainWindow->findChild<QAction*>("actionShowNotes");
  if (showNotesAction->isChecked()) {
    notesArea->show();
  }
  else {
    notesArea->hide();
  }
}

void MainUi::updateNumberOfAddresses() {
  char message[128];
  sprintf(message, "%ld", med.scanAddresses.size());
  mainWindow->findChild<QLabel*>("found")->setText(message);
}

void MainUi::setWindowTitle() {
  if (filename.length() > 0) {
    mainWindow->setWindowTitle(MAIN_TITLE + ": " + filename);
  }
  else {
    mainWindow->setWindowTitle(MAIN_TITLE);
  }
}

void MainUi::onSaveTriggered() {
  if (filename == "") {
    return;
  }
  med.saveFile(filename.toStdString().c_str());
  statusBar->showMessage("Saved");
}

void MainUi::onQuitTriggered() {
  app->quit();
}

void MainUi::onShowNotesTriggered(bool checked) {
  if (checked) {
    notesArea->show();
  }
  else {
    notesArea->hide();
  }
}

void MainUi::onNotesAreaChanged() {
  med.notes = notesArea->toPlainText().toStdString();
}

////////////////////////
// Accessors
////////////////////////

UiState MainUi::getScanState() {
  return scanState;
}
UiState MainUi::getStoreState() {
  return storeState;
}

void MainUi::setScanState(UiState state) {
  scanState = state;
}
void MainUi::setStoreState(UiState state) {
  storeState = state;
}

void MainUi::clearAll() {
  filename = "";
  onScanClearClicked();
  onStoreClearClicked();
  updateNumberOfAddresses();
}

//////////////////////
// Main function
/////////////////////

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  new MainUi(&app);
  return app.exec();
}
