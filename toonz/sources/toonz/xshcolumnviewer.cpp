
#include "xshcolumnviewer.h"

// Tnz6 includes
#include "xsheetviewer.h"
#include "tapp.h"
#include "menubarcommandids.h"
#include "columnselection.h"
#include "xsheetdragtool.h"
#include "tapp.h"

// TnzTools includes
#include "tools/toolhandle.h"
#include "tools/toolcommandids.h"

// TnzQt includes
#include "toonzqt/tselectionhandle.h"
#include "toonzqt/gutil.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/intfield.h"

// TnzLib includes
#include "toonz/tscenehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/stage2.h"
#include "toonz/txshpalettecolumn.h"
#include "toonz/txsheet.h"
#include "toonz/toonzscene.h"
#include "toonz/txshcell.h"
#include "toonz/tstageobject.h"
#include "toonz/tstageobjecttree.h"
#include "toonz/sceneproperties.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/tcolumnfx.h"
#include "toonz/txshsoundcolumn.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/columnfan.h"
#include "toonz/tstageobjectcmd.h"
#include "toonz/fxcommand.h"
#include "toonz/txshleveltypes.h"
#include "toonz/levelproperties.h"
#include "toonz/preferences.h"
#include "toonz/childstack.h"
#include "toonz/txshlevelcolumn.h"
#include "toonz/tfxhandle.h"

// TnzCore includes
#include "tconvert.h"

#include <QApplication>
#include <QMainWindow>
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QToolTip>
#include <QTimer>
#include <QLabel>
#include <QComboBox>
//=============================================================================

namespace {
const QSet<TXshSimpleLevel *> getLevels(TXshColumn *column) {
  QSet<TXshSimpleLevel *> levels;
  TXshCellColumn *cellColumn = column->getCellColumn();
  if (cellColumn) {
    int i, r0, r1;
    cellColumn->getRange(r0, r1);
    for (i = r0; i <= r1; i++) {
      TXshCell cell       = cellColumn->getCell(i);
      TXshSimpleLevel *sl = cell.getSimpleLevel();
      if (sl) levels.insert(sl);
    }
  }
  return levels;
}

bool containsRasterLevel(TColumnSelection *selection) {
  if (!selection || selection->isEmpty()) return false;
  set<int> indexes = selection->getIndices();
  TXsheet *xsh     = TApp::instance()->getCurrentXsheet()->getXsheet();
  set<int>::iterator it;
  for (it = indexes.begin(); it != indexes.end(); it++) {
    TXshColumn *col = xsh->getColumn(*it);
    if (!col || col->getColumnType() != TXshColumn::eLevelType) continue;

    TXshCellColumn *cellCol = col->getCellColumn();
    if (!cellCol) continue;

    int i;
    for (i = 0; i < cellCol->getMaxFrame() + 1; i++) {
      TXshCell cell = cellCol->getCell(i);
      if (cell.isEmpty()) continue;
      TXshSimpleLevel *level = cell.getSimpleLevel();
      if (!level || level->getChildLevel() ||
          level->getProperties()->getDirtyFlag())
        continue;
      int type = level->getType();
      if (type == OVL_XSHLEVEL || type == TZP_XSHLEVEL) return true;
    }
  }
  return false;
}

const QIcon getColorChipIcon(const int id) {
  static QList<QColor> colors = {Qt::red,        Qt::green,    Qt::blue,
                                 Qt::darkYellow, Qt::darkCyan, Qt::darkMagenta};
  QPixmap pixmap(12, 12);
  pixmap.fill(colors.at(id - 1));
  return QIcon(pixmap);
}
}

//-----------------------------------------------------------------------------

namespace XsheetGUI {

//-----------------------------------------------------------------------------

static void getVolumeCursorRect(QRect &out, double volume,
                                const QPoint &origin) {
  int ly = 60;
  int v  = tcrop(0, ly, (int)(volume * ly));
  out.setX(origin.x() + 11);
  out.setY(origin.y() + 60 - v);
  out.setWidth(8);
  out.setHeight(8);
}

//=============================================================================
// MotionPathMenu
//-----------------------------------------------------------------------------

#if QT_VERSION >= 0x050500
MotionPathMenu::MotionPathMenu(QWidget *parent, Qt::WindowFlags flags)
#else
MotionPathMenu::MotionPathMenu(QWidget *parent, Qt::WFlags flags)
#endif
    : QWidget(parent, flags)
    , m_mDeleteRect(QRect(0, 0, ColumnWidth - 13, RowHeight))
    , m_mNormalRect(QRect(0, RowHeight, ColumnWidth - 13, RowHeight))
    , m_mRotateRect(QRect(0, RowHeight * 2, ColumnWidth - 13, RowHeight))
    , m_pos(QPoint()) {
  setMouseTracking(true);
  setFixedSize(ColumnWidth - 12, 3 * RowHeight);
  setWindowFlags(Qt::FramelessWindowHint);
}

//-----------------------------------------------------------------------------

MotionPathMenu::~MotionPathMenu() {}

//-----------------------------------------------------------------------------

void MotionPathMenu::paintEvent(QPaintEvent *) {
  QPainter p(this);

  static QPixmap motionPixmap = QPixmap(":Resources/motionpath.svg");
  static QPixmap motionDeletePixmap =
      QPixmap(":Resources/motionpath_delete.svg");
  static QPixmap motionRotatePixmap = QPixmap(":Resources/motionpath_rot.svg");

  QColor overColor = QColor(49, 106, 197);

  p.fillRect(m_mDeleteRect,
             QBrush((m_mDeleteRect.contains(m_pos)) ? overColor : grey225));
  p.drawPixmap(m_mDeleteRect, motionDeletePixmap);

  p.fillRect(m_mNormalRect,
             QBrush((m_mNormalRect.contains(m_pos)) ? overColor : grey225));
  p.drawPixmap(m_mNormalRect, motionPixmap);

  p.fillRect(m_mRotateRect,
             QBrush((m_mRotateRect.contains(m_pos)) ? overColor : grey225));
  p.drawPixmap(m_mRotateRect, motionRotatePixmap);
}

//-----------------------------------------------------------------------------

void MotionPathMenu::mousePressEvent(QMouseEvent *event) {
  m_pos                   = event->pos();
  TStageObjectId objectId = TApp::instance()->getCurrentObject()->getObjectId();
  TStageObject *pegbar =
      TApp::instance()->getCurrentXsheet()->getXsheet()->getStageObject(
          objectId);

  if (m_mDeleteRect.contains(m_pos))
    pegbar->setStatus(TStageObject::XY);
  else if (m_mNormalRect.contains(m_pos)) {
    pegbar->setStatus(TStageObject::PATH);
    TApp::instance()->getCurrentObject()->setIsSpline(true);
  } else if (m_mRotateRect.contains(m_pos)) {
    pegbar->setStatus(TStageObject::PATH_AIM);
    TApp::instance()->getCurrentObject()->setIsSpline(true);
  }
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  hide();
}

//-----------------------------------------------------------------------------

void MotionPathMenu::mouseMoveEvent(QMouseEvent *event) {
  m_pos = event->pos();
  update();
}

//-----------------------------------------------------------------------------

void MotionPathMenu::mouseReleaseEvent(QMouseEvent *event) {}

//-----------------------------------------------------------------------------

void MotionPathMenu::leaveEvent(QEvent *event) { hide(); }

//=============================================================================
// ChangeObjectWidget
//-----------------------------------------------------------------------------

ChangeObjectWidget::ChangeObjectWidget(QWidget *parent)
    : QListWidget(parent), m_width(40) {
  setMouseTracking(true);
  setObjectName("XshColumnChangeObjectWidget");
  setAutoFillBackground(true);
}

//-----------------------------------------------------------------------------

ChangeObjectWidget::~ChangeObjectWidget() {}

//-----------------------------------------------------------------------------

void ChangeObjectWidget::show(const QPoint &pos) {
  refresh();
  int itemNumber = count();
  if (itemNumber > 10) {
    itemNumber = 10;
    m_width += 15;
  }
  setGeometry(pos.x(), pos.y(), m_width, itemNumber * 16 + 2);
  QListWidget::show();
  setFocus();
  scrollToItem(currentItem());
}

//-----------------------------------------------------------------------------

void ChangeObjectWidget::setObjectHandle(TObjectHandle *objectHandle) {
  m_objectHandle = objectHandle;
}

//-----------------------------------------------------------------------------

void ChangeObjectWidget::setXsheetHandle(TXsheetHandle *xsheetHandle) {
  m_xsheetHandle = xsheetHandle;
}

//-----------------------------------------------------------------------------

void ChangeObjectWidget::mouseMoveEvent(QMouseEvent *event) {
  QListWidgetItem *currentWidgetItem = itemAt(event->pos());
  if (!currentWidgetItem) return;
  clearSelection();
  currentWidgetItem->setSelected(true);
}

//-----------------------------------------------------------------------------

void ChangeObjectWidget::focusOutEvent(QFocusEvent *e) {
  if (!isVisible()) return;
  hide();
  parentWidget()->update();
}

//-----------------------------------------------------------------------------

void ChangeObjectWidget::selectCurrent(const QString &text) {
  QList<QListWidgetItem *> itemList = findItems(text, Qt::MatchExactly);
  clearSelection();
  if (itemList.size() < 1) return;
  QListWidgetItem *currentWidgetItem = itemList.at(0);
  setCurrentItem(currentWidgetItem);
}

//=============================================================================
// ChangeObjectParent
//-----------------------------------------------------------------------------

ChangeObjectParent::ChangeObjectParent(QWidget *parent)
    : ChangeObjectWidget(parent) {
  bool ret = connect(this, SIGNAL(currentTextChanged(const QString &)), this,
                     SLOT(onTextChanged(const QString &)));
  assert(ret);
}

//-----------------------------------------------------------------------------

ChangeObjectParent::~ChangeObjectParent() {}

//-----------------------------------------------------------------------------

void ChangeObjectParent::refresh() {
  clear();
  assert(m_xsheetHandle);
  assert(m_objectHandle);
  TXsheet *xsh                   = m_xsheetHandle->getXsheet();
  TStageObjectId currentObjectId = m_objectHandle->getObjectId();
  TStageObjectId parentId = xsh->getStageObject(currentObjectId)->getParent();
  TStageObjectTree *tree  = xsh->getStageObjectTree();
  int objectCount         = tree->getStageObjectCount();
  QString text;
  QList<QString> pegbarList;
  QList<QString> columnList;
  int maxTextLength = 0;
  int i;
  for (i = 0; i < objectCount; i++) {
    TStageObjectId id = tree->getStageObject(i)->getId();
    int index         = id.getIndex();
    QString indexStr(std::to_string(id.getIndex() + 1).c_str());
    QString newText;
    if (id == parentId) {
      if (parentId.isPegbar())
        text = QString("Peg ") + indexStr;
      else if (parentId.isColumn())
        text = QString("Col ") + indexStr;
    }
    if (id == currentObjectId) continue;
    if (id.isPegbar()) {
      newText = QString("Peg ") + indexStr;
      pegbarList.append(newText);
    }
    if (id.isColumn() && (!xsh->isColumnEmpty(index) || index < 2)) {
      newText = QString("Col ") + indexStr;
      columnList.append(newText);
    }
    if (newText.length() > maxTextLength) maxTextLength = newText.length();
  }
  for (i = 0; i < columnList.size(); i++) addItem(columnList.at(i));
  for (i = 0; i < pegbarList.size(); i++) addItem(pegbarList.at(i));

  m_width = maxTextLength * XSHEET_FONT_SIZE + 2;
  selectCurrent(text);
}

//-----------------------------------------------------------------------------

void ChangeObjectParent::onTextChanged(const QString &text) {
  assert(m_xsheetHandle);
  assert(m_objectHandle);
  if (text.isEmpty()) {
    hide();
    return;
  }
  bool isPegbar                        = false;
  if (text.startsWith("Peg")) isPegbar = true;
  QString number                       = text;
  number.remove(0, 4);
  int index = number.toInt() - 1;
  if (index < 0) {
    hide();
    return;
  }
  TStageObjectId currentObjectId = m_objectHandle->getObjectId();
  TStageObjectId newStageObjectId;
  if (isPegbar)
    newStageObjectId = TStageObjectId::PegbarId(index);
  else
    newStageObjectId = TStageObjectId::ColumnId(index);

  if (newStageObjectId == currentObjectId) return;

  TStageObject *stageObject =
      m_xsheetHandle->getXsheet()->getStageObject(currentObjectId);
  TStageObjectCmd::setParent(currentObjectId, newStageObjectId, "B",
                             m_xsheetHandle);

  hide();
  m_objectHandle->notifyObjectIdChanged(false);
  m_xsheetHandle->notifyXsheetChanged();
}

//=============================================================================
// ChangeObjectHandle
//-----------------------------------------------------------------------------

ChangeObjectHandle::ChangeObjectHandle(QWidget *parent)
    : ChangeObjectWidget(parent) {
  bool ret = connect(this, SIGNAL(currentTextChanged(const QString &)), this,
                     SLOT(onTextChanged(const QString &)));
  assert(ret);
}

//-----------------------------------------------------------------------------

ChangeObjectHandle::~ChangeObjectHandle() {}

//-----------------------------------------------------------------------------

void ChangeObjectHandle::refresh() {
  clear();
  assert(m_xsheetHandle);
  assert(m_objectHandle);
  TXsheet *xsh = m_xsheetHandle->getXsheet();
  assert(xsh);
  TStageObjectId currentObjectId = m_objectHandle->getObjectId();
  TStageObject *stageObject      = xsh->getStageObject(currentObjectId);
  m_width                        = 28;

  int i;
  QString str;
  if (stageObject->getParent().isColumn()) {
    for (i = 0; i < 20; i++) addItem(str.number(20 - i));
  }
  for (i = 0; i < 26; i++) addItem(QString(char('A' + i)));

  std::string handle = stageObject->getParentHandle();
  if (handle[0] == 'H' && handle.length() > 1) handle = handle.substr(1);

  selectCurrent(QString::fromStdString(handle));
}

//-----------------------------------------------------------------------------

void ChangeObjectHandle::onTextChanged(const QString &text) {
  assert(m_xsheetHandle);
  assert(m_objectHandle);
  TStageObjectId currentObjectId = m_objectHandle->getObjectId();
  QString handle                 = text;
  if (text.toInt() != 0) handle  = QString("H") + handle;
  if (handle.isEmpty()) return;
  std::vector<TStageObjectId> ids;
  ids.push_back(currentObjectId);
  TStageObjectCmd::setParentHandle(ids, handle.toStdString(), m_xsheetHandle);
  hide();
  m_objectHandle->notifyObjectIdChanged(false);
  m_xsheetHandle->notifyXsheetChanged();
}

//=============================================================================
// RenameColumnField
//-----------------------------------------------------------------------------

RenameColumnField::RenameColumnField(QWidget *parent, XsheetViewer *viewer)
    : QLineEdit(parent), m_col(-1) {
  setFixedSize(20, 20);
  connect(this, SIGNAL(returnPressed()), SLOT(renameColumn()));
}

//-----------------------------------------------------------------------------

void RenameColumnField::show(const QRect &rect, int col) {
  move(rect.topLeft ());
  setFixedSize (rect.size ());
  static QFont font("Helvetica", XSHEET_FONT_SIZE, QFont::Normal);
  setFont(font);
  m_col = col;

  TXsheet *xsh = m_xsheetHandle->getXsheet();
  std::string name =
      xsh->getStageObject(TStageObjectId::ColumnId(col))->getName();
  TXshColumn *column          = xsh->getColumn(col);
  TXshZeraryFxColumn *zColumn = dynamic_cast<TXshZeraryFxColumn *>(column);
  if (zColumn)
    name = ::to_string(zColumn->getZeraryColumnFx()->getZeraryFx()->getName());
  setText(QString(name.c_str()));
  selectAll();

  QWidget::show();
  raise();
  setFocus();
}

//-----------------------------------------------------------------------------

void RenameColumnField::renameColumn() {
  std::string newName     = text().toStdString();
  TStageObjectId columnId = TStageObjectId::ColumnId(m_col);
  TXshColumn *column =
      m_xsheetHandle->getXsheet()->getColumn(columnId.getIndex());
  TXshZeraryFxColumn *zColumn = dynamic_cast<TXshZeraryFxColumn *>(column);
  if (zColumn)
    TFxCommand::renameFx(zColumn->getZeraryColumnFx(), ::to_wstring(newName),
                         m_xsheetHandle);
  else
    TStageObjectCmd::rename(columnId, newName, m_xsheetHandle);
  m_xsheetHandle->notifyXsheetChanged();
  m_col = -1;
  setText("");
  hide();
}

//-----------------------------------------------------------------------------

void RenameColumnField::focusOutEvent(QFocusEvent *e) {
  std::wstring newName = text().toStdWString();
  if (!newName.empty())
    renameColumn();
  else
    hide();

  QLineEdit::focusOutEvent(e);
}

//-----------------------------------------------------------------------------

ColumnArea::DrawHeader ::DrawHeader (ColumnArea *nArea, QPainter &nP, int nCol)
  : area (nArea), p (nP), col (nCol) {
  m_viewer = area->m_viewer;
  o = m_viewer->orientation ();
  app = TApp::instance ();
  xsh = m_viewer->getXsheet ();
  column = col >= 0 ? xsh->getColumn (col) : 0;
  isEmpty = col >= 0 && xsh->isColumnEmpty (col);

  TStageObjectId currentColumnId = app->getCurrentObject ()->getObjectId ();

  // check if the column is current
  isCurrent = false;
  if (currentColumnId == TStageObjectId::CameraId (0))  // CAMERA
    isCurrent = col == -1;
  else
    isCurrent = m_viewer->getCurrentColumn () == col;

  orig = m_viewer->positionToXY (CellPosition (0, max (col, 0)));
}

void ColumnArea::DrawHeader ::prepare () const {
  // Preparing painter
#ifdef _WIN32
  QFont font ("Arial", XSHEET_FONT_SIZE, QFont::Normal);
#else
  QFont font ("Helvetica", XSHEET_FONT_SIZE, QFont::Normal);
#endif
  p.setFont (font);
  p.setRenderHint (QPainter::SmoothPixmapTransform, true);
}

//-----------------------------------------------------------------------------

void ColumnArea::DrawHeader ::levelColors (QColor &columnColor, QColor &dragColor) const {
  enum { Normal, Reference, Control } usage = Reference;
  if (column) {
    if (column->isControl ()) usage = Control;
    if (column->isRendered () || column->getMeshColumn ()) usage = Normal;
  }

  if (usage == Reference) {
    columnColor = m_viewer->getReferenceColumnColor ();
    dragColor = m_viewer->getReferenceColumnBorderColor ();
  }
  else
    m_viewer->getColumnColor (columnColor, dragColor, col, xsh);
}
void ColumnArea::DrawHeader ::soundColors (QColor &columnColor, QColor &dragColor) const {
  m_viewer->getColumnColor (columnColor, dragColor, col, xsh);
}
void ColumnArea::DrawHeader ::paletteColors (QColor &columnColor, QColor &dragColor) const {
  enum { Normal, Reference, Control } usage = Reference;
  if (column) {  // Check if column is a mask
    if (column->isControl ()) usage = Control;
    if (column->isRendered ()) usage = Normal;
  }

  if (usage == Reference) {
    columnColor = m_viewer->getReferenceColumnColor ();
    dragColor = m_viewer->getReferenceColumnBorderColor ();
  }
  else {
    columnColor = m_viewer->getPaletteColumnColor ();
    dragColor = m_viewer->getPaletteColumnBorderColor ();
  }
}

void ColumnArea::DrawHeader ::drawBaseFill (const QColor &columnColor, const QColor &dragColor) const {
  // check if the column is reference
  bool isEditingSpline = app->getCurrentObject ()->isSpline ();

  QRect rect = o->rect (PredefinedRect::LAYER_HEADER)
    .translated (orig);

  int x0 = rect.left ();
  int x1 = rect.right ();
  int y0 = rect.top ();
  int y1 = rect.bottom ();

  // fill base color
  if (isEmpty || col < 0) {
    p.fillRect (rect, m_viewer->getEmptyColumnHeadColor ());

    p.setPen (m_viewer->getVerticalLineHeadColor ());
    QLine vertical = o->verticalLine (m_viewer->columnToLayerAxis (col), o->frameSide (rect));
    p.drawLine (vertical);
  }
  else {
    p.fillRect (rect, columnColor);

    // column handle
    QRect sideBar = o->rect (PredefinedRect::DRAG_LAYER).translated (x0, y0);
    p.fillRect (sideBar, sideBar.contains (area->m_pos) ? Qt::yellow : dragColor);
  }

  // highlight selection
  bool isSelected =
    m_viewer->getColumnSelection ()->isColumnSelected (col) && !isEditingSpline;
  bool isCameraSelected = col == -1 && isCurrent && !isEditingSpline;

  QColor pastelizer (m_viewer->getColumnHeadPastelizer ());
  pastelizer.setAlpha (50);

  QColor colorSelection (m_viewer->getSelectedColumnHead ());
  colorSelection.setAlpha (170);
  p.fillRect (rect,
    (isSelected || isCameraSelected) ? colorSelection : pastelizer);
}

void ColumnArea::DrawHeader ::drawEye () const {
  if (col < 0 || isEmpty)
    return;
  if (!column->isPreviewVisible ())
    return;

  QRect prevViewRect = o->rect (PredefinedRect::EYE_AREA).translated (orig);
  QRect eyeRect = o->rect (PredefinedRect::EYE).translated (orig);
  static QPixmap prevViewPix = QPixmap (":Resources/x_prev_eye.png");

  // preview visible toggle
  p.fillRect (prevViewRect, PreviewVisibleColor);
  p.drawPixmap (eyeRect, prevViewPix);
}

void ColumnArea::DrawHeader ::drawPreviewToggle (int opacity) const {
  if (col < 0 || isEmpty)
    return;
  // camstand visible toggle
  if (!column->isCamstandVisible ())
    return;

  QRect tableViewRect = o->rect (PredefinedRect::PREVIEW_LAYER_AREA).translated (orig);
  QRect tableViewImgRect = o->rect (PredefinedRect::PREVIEW_LAYER).translated (orig);
  static QPixmap tableViewPix = QPixmap (":Resources/x_table_view.png");
  static QPixmap tableTranspViewPix = QPixmap (":Resources/x_table_view_transp.png");

  p.fillRect (tableViewRect, CamStandVisibleColor);
  p.drawPixmap (tableViewImgRect, opacity < 255
    ? tableTranspViewPix
    : tableViewPix);
}

void ColumnArea::DrawHeader ::drawLock () const {
  if (col < 0 || isEmpty)
    return;

  QRect lockModeRect = o->rect (PredefinedRect::LOCK).translated (orig);
  static QPixmap lockModePix = QPixmap (":Resources/x_lock.png");

  // lock button
  p.setPen (Qt::gray);
  p.setBrush (QColor (255, 255, 255, 128));
  p.drawRect (lockModeRect);
  lockModeRect.adjust (1, 1, -1, -1);
  bool isLocked = column && column->isLocked ();
  if (isLocked)
    p.drawPixmap (lockModeRect, lockModePix);
}

void ColumnArea::DrawHeader ::drawColumnNumber () const {
  if (col < 0 || isEmpty)
    return;

  p.setPen(m_viewer->getTextColor ());
  QRect pos = o->rect (PredefinedRect::COLUMN_NUMBER)
    .translated (orig);
  if (pos.isEmpty ())
    return;
  p.drawText (pos, Qt::AlignCenter | Qt::TextSingleLine,
    QString::number (col + 1));
}

void ColumnArea::DrawHeader ::drawColumnName () const {
  TStageObjectId columnId = m_viewer->getObjectId (col);
  TStageObject *columnObject = xsh->getStageObject (columnId);

  // Build column name
  std::string name (columnObject->getName ());
  if (col < 0) name = std::string ("Camera");

  if (!isEmpty)
    p.setPen((isCurrent) ? Qt::red : Qt::black);
  else
    p.setPen((isCurrent) ? m_viewer->getSelectedColumnTextColor()
                         : m_viewer->getTextColor());

  QRect columnName = o->rect (PredefinedRect::COLUMN_NAME)
    .translated (orig).adjusted (2, 0, -2, 0);
  p.drawText(columnName, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
    QString(name.c_str()));
}

void ColumnArea::DrawHeader ::drawSoundIcon (bool isPlaying) const {
  static QPixmap soundActiveIcon = QPixmap (":Resources/sound_header_on.png");
  static QPixmap soundIcon = QPixmap (":Resources/sound_header_off.png");

  QRect rect = m_viewer->orientation ()->rect (PredefinedRect::SOUND_ICON)
    .translated (orig);
  p.drawPixmap (rect, isPlaying ? soundActiveIcon : soundIcon);
}

//=============================================================================
// ColumnArea
//-----------------------------------------------------------------------------
#if QT_VERSION >= 0x050500
ColumnArea::ColumnArea(XsheetViewer *parent, Qt::WindowFlags flags)
#else
ColumnArea::ColumnArea(XsheetViewer *parent, Qt::WFlags flags)
#endif
    : QWidget(parent, flags)
    , m_viewer(parent)
    , m_indexBox(0, 3, ColumnWidth - RowHeight * 3 - 1, RowHeight)
    , m_tabBox(ColumnWidth - RowHeight * 3 - 1, 3, RowHeight * 3, RowHeight + 1)
    , m_nameBox(0, RowHeight + 3, ColumnWidth, RowHeight)
    , m_linkBox(0, RowHeight * 7 + 3, 12, RowHeight)
    , m_pos(-1, -1)
    , m_tooltip(tr(""))
    , m_col(-1)
    , m_columnTransparencyPopup(0)
    , m_transparencyPopupTimer(0)
    , m_isPanning(false) {
  TXsheetHandle *xsheetHandle = TApp::instance()->getCurrentXsheet();
#ifndef LINETEST
  TObjectHandle *objectHandle = TApp::instance()->getCurrentObject();
  m_changeObjectParent        = new ChangeObjectParent(0);
  m_changeObjectParent->setObjectHandle(objectHandle);
  m_changeObjectParent->setXsheetHandle(xsheetHandle);
  m_changeObjectParent->hide();

  m_changeObjectHandle = new ChangeObjectHandle(0);
  m_changeObjectHandle->setObjectHandle(objectHandle);
  m_changeObjectHandle->setXsheetHandle(xsheetHandle);
  m_changeObjectHandle->hide();
#else
  m_motionPathMenu = new MotionPathMenu(0);
#endif

  m_renameColumnField = new RenameColumnField(this, m_viewer);
  m_renameColumnField->setXsheetHandle(xsheetHandle);
  m_renameColumnField->hide();

  QActionGroup *actionGroup = new QActionGroup(this);
  m_subsampling1            = new QAction(tr("&Subsampling 1"), actionGroup);
  m_subsampling2            = new QAction(tr("&Subsampling 2"), actionGroup);
  m_subsampling3            = new QAction(tr("&Subsampling 3"), actionGroup);
  m_subsampling4            = new QAction(tr("&Subsampling 4"), actionGroup);
  actionGroup->addAction(m_subsampling1);
  actionGroup->addAction(m_subsampling2);
  actionGroup->addAction(m_subsampling3);
  actionGroup->addAction(m_subsampling4);

  connect(actionGroup, SIGNAL(triggered(QAction *)), this,
          SLOT(onSubSampling(QAction *)));

  setMouseTracking(true);
}

//-----------------------------------------------------------------------------

ColumnArea::~ColumnArea() {}

//-----------------------------------------------------------------------------

DragTool *ColumnArea::getDragTool() const { return m_viewer->getDragTool(); }
void ColumnArea::setDragTool(DragTool *dragTool) {
  m_viewer->setDragTool(dragTool);
}

//-----------------------------------------------------------------------------

void ColumnArea::drawLevelColumnHead(QPainter &p, int col) {
  TColumnSelection *selection = m_viewer->getColumnSelection();
  const Orientation *o = m_viewer->orientation ();

  // Retrieve reference coordinates
  int currentColumnIndex = m_viewer->getCurrentColumn();
  int layerAxis          = m_viewer->columnToLayerAxis(col);

  QPoint orig = m_viewer->positionToXY (CellPosition (0, col));
  QRect rect = o->rect (PredefinedRect::LAYER_HEADER)
    .translated (orig);

  TApp *app    = TApp::instance();
  TXsheet *xsh = m_viewer->getXsheet();

  TStageObjectId columnId        = m_viewer->getObjectId(col);
  TStageObjectId currentColumnId = app->getCurrentObject()->getObjectId();
  TStageObjectId parentId        = xsh->getStageObjectParent(columnId);

  // Retrieve column properties
  // Check if the column is empty
  bool isEmpty = col >= 0 && xsh->isColumnEmpty (col);
  TXshColumn *column = col >= 0 ? xsh->getColumn (col) : 0;

  bool isEditingSpline = app->getCurrentObject()->isSpline();

  // check if the column is current
  bool isCurrent = false;
  if (currentColumnId == TStageObjectId::CameraId(0))  // CAMERA
    isCurrent = col == -1;
  else
    isCurrent = m_viewer->getCurrentColumn() == col;

  bool isSelected =
      m_viewer->getColumnSelection()->isColumnSelected(col) && !isEditingSpline;
  bool isCameraSelected = col == -1 && isCurrent && !isEditingSpline;

  // Draw column
  QPoint pegbarNamePos = orig + QPoint(12, RowHeight * 3 + 48);
  QPoint handleNamePos =
      orig +
      QPoint(ColumnWidth - 10 - p.fontMetrics().width('B'), RowHeight * 3 + 48);

  DrawHeader drawHeader (this, p, col);
  drawHeader.prepare ();
  QColor columnColor, dragColor;
  drawHeader.levelColors (columnColor, dragColor);
  drawHeader.drawBaseFill (columnColor, dragColor);
  drawHeader.drawEye ();
  drawHeader.drawPreviewToggle (column ? column->getOpacity () : 0);
  drawHeader.drawLock ();
  drawHeader.drawColumnNumber ();
  drawHeader.drawColumnName ();

  p.setPen(m_viewer->getTextColor());

  if (col >= 0 && !isEmpty) {
    // pegbar name
    p.drawText(pegbarNamePos, QString(parentId.toString().c_str()));

    std::string handle = xsh->getStageObject(columnId)->getParentHandle();
    if (handle[0] == 'H' && handle.length() > 1) handle = handle.substr(1);
    if (parentId != TStageObjectId::TableId)
      p.drawText(handleNamePos, QString::fromStdString(handle));

    // thumbnail
    QRect thumbnailRect(orig.x() + 9, orig.y() + RowHeight * 2 + 7,
                        ColumnWidth - 11, 42);

    // for zerary fx, display fxId here instead of thumbnail
    TXshZeraryFxColumn *zColumn = dynamic_cast<TXshZeraryFxColumn *>(column);
    if (zColumn) {
      QFont font("Verdana", 8);
      p.setFont(font);

      TFx *fx        = zColumn->getZeraryColumnFx()->getZeraryFx();
      QString fxName = QString::fromStdWString(fx->getFxId());
      p.drawText(thumbnailRect, Qt::TextWrapAnywhere | Qt::TextWordWrap,
                 fxName);
    } else {
      TXshLevelColumn *levelColumn = column->getLevelColumn();

      if (levelColumn &&
          Preferences::instance()->getColumnIconLoadingPolicy() ==
              Preferences::LoadOnDemand &&
          !levelColumn->isIconVisible()) {
        // display nothing
      } else {
        QPixmap iconPixmap = getColumnIcon(col);
        if (!iconPixmap.isNull()) {
          p.drawPixmap(thumbnailRect, iconPixmap);
        }
        // notify that the column icon is already shown
        if (levelColumn) levelColumn->setIconVisible(true);
      }

      // filter color
      if (column->getFilterColorId() != 0) {
        QRect filterColorRect(thumbnailRect.topRight().x() - 14,
                              thumbnailRect.topRight().y(), 14, 14);
        p.fillRect(filterColorRect, Qt::white);
        p.drawPixmap(
            filterColorRect.adjusted(2, 2, -2, -2),
            getColorChipIcon(column->getFilterColorId()).pixmap(12, 12));
      }
    }
  }
}

//-----------------------------------------------------------------------------

void ColumnArea::drawSoundColumnHead(QPainter &p, int col) { // AREA
  TColumnSelection *selection = m_viewer->getColumnSelection();

  int x = m_viewer->columnToLayerAxis (col);

  TXsheet *xsh = m_viewer->getXsheet();
  TXshSoundColumn *sc =
      xsh->getColumn(col) ? xsh->getColumn(col)->getSoundColumn() : 0;

  QPoint orig = m_viewer->positionToXY (CellPosition (0, col));
  QRect rect = m_viewer->orientation ()->rect (PredefinedRect::LAYER_HEADER)
    .translated (orig);

  QPoint columnNamePos = orig + QPoint(12, RowHeight);

  bool isCurrent  = m_viewer->getCurrentColumn() == col;

  DrawHeader drawHeader (this, p, col);
  drawHeader.prepare ();
  QColor columnColor, dragColor;
  drawHeader.soundColors (columnColor, dragColor);
  drawHeader.drawBaseFill (columnColor, dragColor);
  drawHeader.drawEye ();
  drawHeader.drawPreviewToggle (255);
  drawHeader.drawLock ();
  drawHeader.drawColumnNumber ();
  drawHeader.drawColumnName ();

  drawHeader.drawSoundIcon (sc->isPlaying ());

  QRect rr(rect.x() + 8, RowHeight * 2 + 3, rect.width() - 7, m_tabBox.y() - 3);

  // slider subdivisions
  p.setPen(m_viewer->getTextColor());
  int xa = rr.x() + 7, ya = rr.y() + 4;
  int y = ya;
  for (int i = 0; i <= 20; i++, y += 3)
    if ((i % 10) == 0)
      p.drawLine(xa - 3, y, xa, y);
    else if (i & 1)
      p.drawLine(xa, y, xa, y);
    else
      p.drawLine(xa - 2, y, xa, y);

  // slider
  int ly = 60;
  xa += 5;
  p.drawPoint(xa, ya);
  p.drawPoint(xa, ya + ly);
  p.drawLine(xa - 1, ya + 1, xa - 1, ya + ly - 1);
  p.drawLine(xa + 1, ya + 1, xa + 1, ya + ly - 1);

  // cursor
  QRect cursorRect;
  getVolumeCursorRect(cursorRect, sc->getVolume(), rr.topLeft());

  std::vector<QPointF> pts;
  x = cursorRect.x();
  y = cursorRect.y() + 4;
  pts.push_back(QPointF(x, y));
  pts.push_back(QPointF(x + 4.0, y + 4.0));
  pts.push_back(QPointF(x + 8.0, y + 4.0));
  pts.push_back(QPointF(x + 8.0, y - 4.0));
  pts.push_back(QPointF(x + 4.0, y - 4.0));
  drawPolygon(p, pts, true, m_viewer->getLightLineColor());
}

//-----------------------------------------------------------------------------

void ColumnArea::drawPaletteColumnHead(QPainter &p, int col) { // AREA
  TColumnSelection *selection = m_viewer->getColumnSelection();

  QPoint orig = m_viewer->positionToXY (CellPosition (0, max (col, 0)));

  TXsheet *xsh = m_viewer->getXsheet();

  bool isEmpty = false;
  if (col >= 0)  // Verifico se la colonna e' vuota
    isEmpty            = xsh->isColumnEmpty(col);

  DrawHeader drawHeader (this, p, col);
  drawHeader.prepare ();
  QColor columnColor, dragColor;
  drawHeader.paletteColors (columnColor, dragColor);
  drawHeader.drawBaseFill (columnColor, dragColor);
  drawHeader.drawEye ();
  drawHeader.drawLock ();
  drawHeader.drawColumnNumber ();
  drawHeader.drawColumnName ();

  // pallete icon
  p.setPen(Qt::black);
  if (col >= 0 && !isEmpty) {
    static QPixmap paletteHeader(":Resources/palette_header.png");
    QRect thumbnailRect(orig.x() + 9, orig.y() + RowHeight * 2 + 7,
                        ColumnWidth - 11, 42);
    p.drawPixmap(thumbnailRect, paletteHeader);
  }
}

//-----------------------------------------------------------------------------

void ColumnArea::drawSoundTextColumnHead(QPainter &p, int col) { // AREA
  TColumnSelection *selection = m_viewer->getColumnSelection();

  int x = m_viewer->columnToLayerAxis (col);
  QRect rect(x, 0, ColumnWidth, height());

  int x0, x1, y, y0, y1;

  TApp *app    = TApp::instance();
  TXsheet *xsh = m_viewer->getXsheet();

  TStageObjectId columnId = m_viewer->getObjectId(col);
  std::string name        = xsh->getStageObject(columnId)->getName();

  bool isEditingSpline = app->getCurrentObject()->isSpline();

  // Check if column is locked and selected
  TXshColumn *column = col >= 0 ? xsh->getColumn(col) : 0;
  bool isLocked      = column != 0 && column->isLocked();
  bool isCurrent     = m_viewer->getCurrentColumn() == col;
  bool isSelected =
      m_viewer->getColumnSelection()->isColumnSelected(col) && !isEditingSpline;

  QPoint orig = rect.topLeft();
  x0          = rect.x() + 1;
  x1          = orig.x() + m_tabBox.x() + m_tabBox.width();
  y           = orig.y() + m_tabBox.height();

  DrawHeader drawHeader (this, p, col);
  drawHeader.prepare ();

  static QPixmap header(":Resources/magpie.png");
  int iconW = header.width();
  int iconH = header.height();
  QRect iconBox(orig.x() + m_nameBox.x(),
                orig.y() + m_nameBox.y() + m_nameBox.height() + 2, iconW + 1,
                iconH + 1);
  p.drawPixmap(iconBox, header);

  bool isPrecedentColSelected =
      selection->isColumnSelected(col - 1) && !isEditingSpline;
  // bordo sinistro
  if (isSelected || col > 0 && isPrecedentColSelected) {
    p.setPen(ColorSelection);
    p.drawLine(rect.x(), orig.y(), rect.x(), height());
    p.setPen(m_viewer->getDarkLineColor());
    p.drawLine(rect.x(), orig.y(), rect.x(), y + 2);
  } else {
    p.setPen(m_viewer->getDarkLineColor());
    p.drawLine(rect.x(), orig.y(), rect.x(), height());
  }

  if (col >= 0) {
    // sfondo della parte indice
    QRect indexBox(orig.x() + 1, orig.y(), m_indexBox.width(),
                   m_indexBox.height() + 3);
    p.fillRect(indexBox, m_viewer->getDarkBGColor());
    // indice colonna in alto a sinistra
    p.setPen(isCurrent ? Qt::red : Qt::black);
    p.drawText(indexBox.adjusted(0, 2, -2, 0), Qt::AlignRight,
               QString(std::to_string(col + 1).c_str()));

    x0     = orig.x() + m_tabBox.x() + 1;
    int x1 = x0 + RowHeight;
    int x2 = x0 + 2 * RowHeight;
    int x3 = x0 + 3 * RowHeight + 2;
    y0     = orig.y() + m_tabBox.y();
    y1     = orig.y() + m_tabBox.height() + 1;
    // Sfondo dei due bottoni che non vengono mostrati
    p.fillRect(QRect(x0, y0, 2 * RowHeight, RowHeight),
               m_viewer->getDarkBGColor());
    p.setPen(m_viewer->getDarkBGColor());
    p.drawLine(x0, y0 - 1, x3, y0 - 1);
    p.drawLine(x0, y0 - 2, x3, y0 - 2);

    // Linea di separazione tra indice e nome
    p.setPen(m_viewer->getDarkLineColor());
    p.drawLine(orig.x(), y1 + 1, orig.x() + ColumnWidth, y1 + 1);
    // contorno del bottone in alto a dx
    p.drawLine(x2, y0, x2, y1);
    p.drawLine(x2, y0, x3, y0);

    // lucchetto
    QRect lockBox(x2 + 1, y0 + 1, 11, 11);
    p.fillRect(lockBox, QBrush(m_viewer->getLightLightBGColor()));
    if (isLocked) {
      static QPixmap lockMode = QPixmap(":Resources/lock_toggle.png");
      p.drawPixmap(lockBox, lockMode);
    }
  }

  // nome colonna
  QColor cellColor = m_viewer->getLightLightBGColor();
  QColor dummyColor;
  m_viewer->getColumnColor(cellColor, dummyColor, col, xsh);
  QRect nameBox(orig.x() + m_nameBox.x() + 1, orig.y() + m_nameBox.y() + 1,
                m_nameBox.width() - 1, m_nameBox.height());
  QColor columnColor = (isSelected) ? ColorSelection : cellColor;
  p.fillRect(nameBox, QBrush(columnColor));
  p.setPen(isCurrent ? Qt::red : Qt::black);
  p.drawText(nameBox.adjusted(3, -1, -3, 0), Qt::AlignLeft,
             QString(name.c_str()));  // Adjusted to match with lineEdit

  // separazione fra nome e icona
  p.setPen(isSelected ? ColorSelection : m_viewer->getLightLineColor());
  x0 = nameBox.x();
  x1 = x0 + nameBox.width();
  y0 = nameBox.y() + nameBox.height();
  p.drawLine(x0, y0, x1, y0);

  if (isSelected) {
    QRect box(x0, y0 + 1, ColumnWidth, height() - 3 * RowHeight - 6);
    QRect adjustBox = box.adjusted(0, 0, -2, -1);
    p.setPen(ColorSelection);
    p.drawRect(adjustBox);
  }
}

//-----------------------------------------------------------------------------

QPixmap ColumnArea::getColumnIcon(int columnIndex) {
  if (columnIndex == -1) {  // Indice colonna = -1 -> CAMERA
    TApp *app             = TApp::instance();
    static QPixmap camera = QPixmap(":Resources/camera.png");
    return camera;
  }
  TXsheet *xsh = m_viewer->getXsheet();
  if (!xsh) return QPixmap();
  if (xsh->isColumnEmpty(columnIndex)) return QPixmap();
  int r0, r1;
  xsh->getCellRange(columnIndex, r0, r1);
  if (r0 > r1) return QPixmap();
  TXshCell cell = xsh->getCell(r0, columnIndex);
  TXshLevel *xl = cell.m_level.getPointer();
  if (!xl)
    return QPixmap();
  else {
    bool onDemand = false;
    if (Preferences::instance()->getColumnIconLoadingPolicy() ==
        Preferences::LoadOnDemand)
      onDemand = m_viewer->getCurrentColumn() != columnIndex;
    QPixmap icon =
        IconGenerator::instance()->getIcon(xl, cell.m_frameId, false, onDemand);
#ifndef LINETEST
    return scalePixmapKeepingAspectRatio(
        icon, QSize(ColumnWidth, height() - 3 * RowHeight - 8));
#else
    return scalePixmapKeepingAspectRatio(
        icon, QSize(ColumnWidth, height() - 4 * RowHeight - 8));
#endif
  }
}

//-----------------------------------------------------------------------------

void ColumnArea::paintEvent(QPaintEvent *event) { // AREA
  QRect toBeUpdated = event->rect();

  QPainter p(this);
  p.setClipRect(toBeUpdated);

  CellRange cellRange = m_viewer->xyRectToRange (toBeUpdated);
  int c0, c1;  // range of visible columns
  c0 = cellRange.from ().layer ();
  c1 = cellRange.to ().layer ();

  TXsheet *xsh         = m_viewer->getXsheet();
  ColumnFan *columnFan = xsh->getColumnFan(m_viewer->orientation ());
  int col;
  for (col = c0; col <= c1; col++) {
    // draw column fan (collapsed columns)
    if (!columnFan->isActive(col)) {
      int x = m_viewer->columnToLayerAxis (col);
      QRect rect(x, 0, 8, height());
      int x0 = rect.topLeft().x() + 1;
      int y  = 16;

      p.setPen(m_viewer->getDarkLineColor());
      p.fillRect(x0, 0, 8, 18, QBrush(m_viewer->getDarkBGColor()));
      p.fillRect(x0, y, 2, 84, QBrush(m_viewer->getLightLightBGColor()));
      p.fillRect(x0 + 3, y + 3, 2, 82,
                 QBrush(m_viewer->getLightLightBGColor()));
      p.fillRect(x0 + 6, y, 2, 84, QBrush(m_viewer->getLightLightBGColor()));

      p.setPen(m_viewer->getDarkLineColor());
      p.drawLine(x0 - 1, y, x0 - 1, rect.height());
      p.drawLine(x0 + 2, y, x0 + 2, rect.height());
      p.drawLine(x0 + 5, y, x0 + 5, rect.height());
      p.drawLine(x0, y, x0 + 1, y);
      p.drawLine(x0 + 3, y + 3, x0 + 4, y + 3);
      p.drawLine(x0 + 6, y, x0 + 7, y);

      // triangolini
      p.setPen(Qt::black);
      x = x0;
      y = y - 4;
      p.drawPoint(QPointF(x, y));
      x++;
      p.drawLine(x, y - 1, x, y + 1);
      x++;
      p.drawLine(x, y - 2, x, y + 2);
      x += 3;
      p.drawLine(x, y - 2, x, y + 2);
      x++;
      p.drawLine(x, y - 1, x, y + 1);
      x++;
      p.drawPoint(x, y);
    } else if (col >= 0) {
      TXshColumn *column = m_viewer->getXsheet()->getColumn(col);

      int colType = (column && !column->isEmpty()) ? column->getColumnType()
                                                   : TXshColumn::eLevelType;

      switch (colType) {
      case TXshColumn::ePaletteType:
        drawPaletteColumnHead(p, col);
        break;
      case TXshColumn::eSoundType:
        drawSoundColumnHead(p, col);
        break;
      case TXshColumn::eSoundTextType:
        drawSoundTextColumnHead(p, col);
        break;
      default:
        drawLevelColumnHead(p, col);
        break;
      }
    }
  }

  p.setPen(grey150);
  p.setBrush(Qt::NoBrush);
  p.drawRect(toBeUpdated.adjusted(0, 0, -1, -1));

  if (getDragTool()) getDragTool()->drawColumnsArea(p);
}

//-----------------------------------------------------------------------------
using namespace DVGui;

ColumnTransparencyPopup::ColumnTransparencyPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup) {
  setFixedWidth(8 + 30 + 8 + 100 + 8 + 8 + 8 - 4);

  m_slider = new QSlider(Qt::Horizontal, this);
  m_slider->setMinimum(1);
  m_slider->setMaximum(100);
  m_slider->setFixedHeight(14);
  m_slider->setFixedWidth(100);

  m_value = new DVGui::IntLineEdit(this, 1, 1, 100);
  /*m_value->setValidator(new QIntValidator (1, 100, m_value));
m_value->setFixedHeight(16);
m_value->setFixedWidth(30);
static QFont font("Helvetica", 7, QFont::Normal);
m_value->setFont(font);*/

  m_filterColorCombo = new QComboBox(this);
  m_filterColorCombo->addItem(tr("None"), 0);
  m_filterColorCombo->addItem(getColorChipIcon(1), tr("Red"), 1);
  m_filterColorCombo->addItem(getColorChipIcon(2), tr("Green"), 2);
  m_filterColorCombo->addItem(getColorChipIcon(3), tr("Blue"), 3);
  m_filterColorCombo->addItem(getColorChipIcon(4), tr("DarkYellow"), 4);
  m_filterColorCombo->addItem(getColorChipIcon(5), tr("DarkCyan"), 5);
  m_filterColorCombo->addItem(getColorChipIcon(6), tr("DarkMagenta"), 6);
  // For now the color filter affects only for Raster and ToonzRaser levels.
  // TODO: Make this property to affect vector levels as well.
  m_filterColorCombo->setToolTip(
      tr("N.B. Filter doesn't affect vector levels"));

  QLabel *filterLabel = new QLabel(tr("Filter:"), this);
  filterLabel->setToolTip(tr("N.B. Filter doesn't affect vector levels"));

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setMargin(3);
  mainLayout->setSpacing(3);
  {
    QHBoxLayout *hlayout = new QHBoxLayout;
    // hlayout->setContentsMargins(0, 3, 0, 3);
    hlayout->setMargin(0);
    hlayout->setSpacing(1);
    hlayout->addWidget(m_slider);
    hlayout->addWidget(m_value);
    hlayout->addWidget(new QLabel("%"));
    mainLayout->addLayout(hlayout, 0);

    QHBoxLayout *filterColorLay = new QHBoxLayout();
    filterColorLay->setMargin(0);
    filterColorLay->setSpacing(2);
    {
      filterColorLay->addWidget(filterLabel, 0);
      filterColorLay->addWidget(m_filterColorCombo, 1);
    }
    mainLayout->addLayout(filterColorLay, 0);
  }
  setLayout(mainLayout);

  bool ret = connect(m_slider, SIGNAL(sliderReleased()), this,
                     SLOT(onSliderReleased()));
  ret = ret && connect(m_slider, SIGNAL(sliderMoved(int)), this,
                       SLOT(onSliderChange(int)));
  ret = ret && connect(m_slider, SIGNAL(valueChanged(int)), this,
                       SLOT(onSliderValueChanged(int)));
  ret = ret && connect(m_value, SIGNAL(textChanged(const QString &)), this,
                       SLOT(onValueChanged(const QString &)));

  ret = ret && connect(m_filterColorCombo, SIGNAL(activated(int)), this,
                       SLOT(onFilterColorChanged(int)));
  assert(ret);
}

//----------------------------------------------------------------

void ColumnTransparencyPopup::onSliderValueChanged(int val) {
  if (m_slider->isSliderDown()) return;
  m_value->setText(QString::number(val));
  onSliderReleased();
}

void ColumnTransparencyPopup::onSliderReleased() {
  m_column->setOpacity(troundp(255.0 * m_slider->value() / 100.0));
  TApp::instance()->getCurrentScene()->notifySceneChanged();
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  ((ColumnArea *)parent())->update();
}

//-----------------------------------------------------------------------

void ColumnTransparencyPopup::onSliderChange(int val) {
  disconnect(m_value, SIGNAL(textChanged(const QString &)), 0, 0);
  m_value->setText(QString::number(val));
  connect(m_value, SIGNAL(textChanged(const QString &)), this,
          SLOT(onValueChanged(const QString &)));
}

//----------------------------------------------------------------

void ColumnTransparencyPopup::onValueChanged(const QString &str) {
  int val = str.toInt();
  m_slider->setValue(val);
  m_column->setOpacity(troundp(255.0 * val / 100.0));

  TApp::instance()->getCurrentScene()->notifySceneChanged();
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  ((ColumnArea *)parent())->update();
}

//----------------------------------------------------------------

void ColumnTransparencyPopup::onFilterColorChanged(int id) {
  m_column->setFilterColorId(id);
  TApp::instance()->getCurrentScene()->notifySceneChanged();
  TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  ((ColumnArea *)parent())->update();
}

//----------------------------------------------------------------

void ColumnTransparencyPopup::setColumn(TXshColumn *column) {
  m_column = column;
  assert(m_column);
  int val = (int)troundp(100.0 * m_column->getOpacity() / 255.0);
  m_slider->setValue(val);
  disconnect(m_value, SIGNAL(textChanged(const QString &)), 0, 0);
  m_value->setText(QString::number(val));
  connect(m_value, SIGNAL(textChanged(const QString &)), this,
          SLOT(onValueChanged(const QString &)));

  m_filterColorCombo->setCurrentIndex(m_column->getFilterColorId());
}

/*void ColumnTransparencyPopup::mouseMoveEvent ( QMouseEvent * e )
{
        int val = tcrop((e->pos().x()+10)/(this->width()/(99-1+1)), 1, 99);
        m_value->setText(QString::number(val));
        m_slider->setValue(val);
}*/

void ColumnTransparencyPopup::mouseReleaseEvent(QMouseEvent *e) {
  // hide();
}

//------------------------------------------------------------------------------

void ColumnArea::openTransparencyPopup() {
  if (m_transparencyPopupTimer) m_transparencyPopupTimer->stop();
  if (m_col < 0) return;
  TXshColumn *column = m_viewer->getXsheet()->getColumn(m_col);
  if (!column || column->isEmpty()) return;

  if (!column->isCamstandVisible()) {
    column->setCamstandVisible(true);
    TApp::instance()->getCurrentScene()->notifySceneChanged();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    update();
  }

  m_columnTransparencyPopup->setColumn(column);
  m_columnTransparencyPopup->show();
}

//----------------------------------------------------------------

void ColumnArea::startTransparencyPopupTimer(QMouseEvent *e) { // AREA
  if (!m_columnTransparencyPopup)
    m_columnTransparencyPopup = new ColumnTransparencyPopup(
        this);  // Qt::ToolTip|Qt::MSWindowsFixedSizeDialogHint);//Qt::MSWindowsFixedSizeDialogHint|Qt::Tool);

  int x = e->pos().x() - m_tabBox.left() - m_viewer->columnToLayerAxis(m_col); // what is happening here?
  int y = e->pos().y() - m_tabBox.bottom();
  m_columnTransparencyPopup->move(e->globalPos().x() - x + 2,
                                  e->globalPos().y() - y + 1);

  if (!m_transparencyPopupTimer) {
    m_transparencyPopupTimer = new QTimer(this);
    bool ret = connect(m_transparencyPopupTimer, SIGNAL(timeout()), this,
                       SLOT(openTransparencyPopup()));
    assert(ret);
    m_transparencyPopupTimer->setSingleShot(true);
  }

  m_transparencyPopupTimer->start(300);
}

//----------------------------------------------------------------

void ColumnArea::mousePressEvent(QMouseEvent *event) {
  const Orientation *o = m_viewer->orientation ();

  m_doOnRelease = 0;
  m_viewer->setQtModifiers(event->modifiers());
  assert(getDragTool() == 0);

  m_col = -1;  // new in 6.4

  // both left and right click can change the selection
  if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
    TXsheet *xsh = m_viewer->getXsheet();
    ColumnFan *fan = xsh->getColumnFan (o);
    m_col = m_viewer->xyToPosition (event->pos ()).layer ();
    // do nothing for the camera column
    if (m_col < 0)  // CAMERA
    {
      TApp::instance()->getCurrentSelection()->getSelection()->makeNotCurrent();
      m_viewer->getColumnSelection()->selectNone();
    }
    // when clicking the column fan
    else if (m_col >= 0 && !fan->isActive(m_col))  // column Fan
    {
      for (auto o : orientations.all ()) {
        fan = xsh->getColumnFan (o);
        for (int i = m_col; i >= 0 && !fan->isActive (i); i--)
          fan->activate (i);
      }

      TApp::instance ()->getCurrentScene ()->setDirtyFlag (true);
      TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
      return;
    }
    // set the clicked column to current
    else
      m_viewer->setCurrentColumn(m_col);

    TXshColumn *column = xsh->getColumn(m_col);
    bool isEmpty       = !column || column->isEmpty();
    TApp::instance()->getCurrentObject()->setIsSpline(false);

    // get mouse position
	  QPoint mouseInCell = event->pos () - m_viewer->positionToXY (CellPosition (0, m_col));
    //int x = event->pos().x() - m_viewer->columnToX(m_col);
    //int y = event->pos().y();
    //QPoint mouseInCell(x, y);
	  int x = mouseInCell.x (), y = mouseInCell.y ();

    if (!isEmpty && m_col >= 0) {
      // grabbing the left side of the column enables column move
      if (o->rect (PredefinedRect::DRAG_LAYER).contains (mouseInCell)) {
        setDragTool(XsheetGUI::DragTool::makeColumnMoveTool(m_viewer));
      }
      // lock button
      else if (o->rect (PredefinedRect::LOCK).contains(mouseInCell) &&
               event->button() == Qt::LeftButton) {
        m_doOnRelease = ToggleLock;
      }
      // preview button
      else if (o->rect (PredefinedRect::EYE_AREA).contains(mouseInCell) &&
               event->button() == Qt::LeftButton) {
        m_doOnRelease = TogglePreviewVisible;
        if (column->getSoundColumn())
          TApp::instance()->getCurrentXsheet()->notifyXsheetSoundChanged();
      }
      // camstand button
      else if (o->rect (PredefinedRect::PREVIEW_LAYER_AREA).contains(mouseInCell) &&
               event->button() == Qt::LeftButton) {
        m_doOnRelease = ToggleTransparency;
        if (column->getSoundColumn()) {
          // do nothing
        } else
          startTransparencyPopupTimer(event);
      }

      // sound column
      else if (column && column->getSoundColumn()) {
        if (o->rect (PredefinedRect::SOUND_ICON).contains (mouseInCell)) {
          TXshSoundColumn *s = column->getSoundColumn();
          if (s) {
            if (s->isPlaying())
              s->stop();
            else {
              s->play();
              if (!s->isPlaying())
                s->stop();  // Serve per vista, quando le casse non sono
                            // attaccate
            }
            int interval = 0;
            if (s->isPlaying()) {
              TSoundTrackP sTrack = s->getCurrentPlaySoundTruck();
              interval            = sTrack->getDuration() * 1000 + 300;
            }
            if (s->isPlaying() && interval > 0)
              QTimer::singleShot(interval, this, SLOT(update()));
          }
          update();
        } else if (x >= 15 && x <= 25 && RowHeight * 2 + 4 < y &&
                   y < 8 * RowHeight + 4)
          setDragTool(XsheetGUI::DragTool::makeVolumeDragTool(m_viewer));
        else
          setDragTool(XsheetGUI::DragTool::makeColumnSelectionTool(m_viewer));
      } else if (column && column->getSoundTextColumn()) {
        if (y < m_tabBox.bottom() || m_nameBox.contains(x, y))
          setDragTool(XsheetGUI::DragTool::makeColumnMoveTool(m_viewer));
        else
          setDragTool(XsheetGUI::DragTool::makeColumnSelectionTool(m_viewer));
      }
      // clicking another area means column selection
      else {
        if (m_viewer->getColumnSelection()->isColumnSelected(m_col) &&
            event->button() == Qt::RightButton)
          return;

        setDragTool(XsheetGUI::DragTool::makeColumnSelectionTool(m_viewer));

        // toggle columnIcon visibility with alt+click
        TXshLevelColumn *levelColumn = column->getLevelColumn();
        if (levelColumn &&
            Preferences::instance()->getColumnIconLoadingPolicy() ==
                Preferences::LoadOnDemand &&
            (event->modifiers() & Qt::AltModifier)) {
          levelColumn->setIconVisible(!levelColumn->isIconVisible());
        }
      }
      // synchronize the current column and the current fx
      TApp::instance()->getCurrentFx()->setFx(column->getFx());
    } else if (m_col >= 0) {
      setDragTool(XsheetGUI::DragTool::makeColumnSelectionTool(m_viewer));
      TApp::instance()->getCurrentFx()->setFx(0);
    }

    m_viewer->dragToolClick(event);
    update();

  } else if (event->button() == Qt::MidButton) {
    m_pos       = event->pos();
    m_isPanning = true;
  }
}

//-----------------------------------------------------------------------------

void ColumnArea::mouseMoveEvent(QMouseEvent *event) {
  const Orientation *o = m_viewer->orientation ();

  m_viewer->setQtModifiers(event->modifiers());
  QPoint pos = event->pos();

  if (m_isPanning) {  // Pan tasto centrale
    QPoint delta = m_pos - pos;
    delta.setY(0);
    m_viewer->scroll(delta);
    return;
  }

  int col            = m_viewer->xyToPosition(pos).layer ();
  if (col < -1) col  = 0;
  TXsheet *xsh       = m_viewer->getXsheet();
  TXshColumn *column = xsh->getColumn(col);
  QPoint mouseInCell = pos - m_viewer->positionToXY (CellPosition (0, col));
  int x = mouseInCell.x (), y = mouseInCell.y ();

#ifdef LINETEST
  // Ensure that the menu of the motion path is hidden
  if ((x - m_mtypeBox.left() > 20 || y < m_mtypeBox.y() ||
       y > m_mtypeBox.bottom()) &&
      !m_motionPathMenu->isHidden())
    m_motionPathMenu->hide();
#endif
  if ((event->buttons() & Qt::LeftButton) != 0 &&
      !visibleRegion().contains(pos)) {
    QRect bounds = visibleRegion().boundingRect();
    m_viewer->setAutoPanSpeed(bounds, pos);
  } else
    m_viewer->stopAutoPan();

  m_pos = pos;

  if (event->buttons() && getDragTool()) {
    m_viewer->dragToolDrag(event);
    update();
    return;
  }

  // Setto i toolTip
  TStageObjectId columnId = m_viewer->getObjectId(col);
  TStageObjectId parentId = xsh->getStageObjectParent(columnId);

  if (col < 0) m_tooltip = tr("Click to select camera");
  if (column && column->getSoundTextColumn())
    m_tooltip = tr("");
  else if (o->rect (PredefinedRect::DRAG_LAYER).contains(mouseInCell)) {
    m_tooltip = tr("Click to select column, drag to move it");
  } else if (o->rect (PredefinedRect::LOCK).contains(mouseInCell)) {
    m_tooltip = tr("Lock Toggle");
  } else if (o->rect (PredefinedRect::EYE_AREA).contains(mouseInCell)) {
    m_tooltip = tr("Preview Visibility Toggle");
  } else if (o->rect (PredefinedRect::PREVIEW_LAYER_AREA).contains(mouseInCell)) {
    m_tooltip = tr("Camera Stand Visibility Toggle");
  } else {
    if (column && column->getSoundColumn()) {
      // sound column
      if (x > 20 && 3 * RowHeight + 5 <= y && y < 3 * RowHeight + 33)
        m_tooltip = tr("Click to play the soundtrack back");
      else if (x >= 10 && x <= 20 && RowHeight + RowHeight / 2 < y &&
               y < 8 * RowHeight - RowHeight / 2)
        m_tooltip = tr("Set the volume of the soundtrack");
    }

    else if (Preferences::instance()->getColumnIconLoadingPolicy() ==
             Preferences::LoadOnDemand)
      m_tooltip = tr("Alt + Click to Toggle Thumbnail");
    else
      m_tooltip = tr("");
  }
  update();
}

//-----------------------------------------------------------------------------

bool ColumnArea::event(QEvent *event) {
  if (event->type() == QEvent::ToolTip) {
    if (!m_tooltip.isEmpty())
      QToolTip::showText(mapToGlobal(m_pos), m_tooltip);
    else
      QToolTip::hideText();
  }
  return QWidget::event(event);
}

//-----------------------------------------------------------------------------

void ColumnArea::mouseReleaseEvent(QMouseEvent *event) {
  TApp *app = TApp::instance();
  if (m_doOnRelease != 0 && m_col != -1) {
    TXshColumn *column = m_viewer->getXsheet()->getColumn(m_col);
    if (m_doOnRelease == ToggleTransparency &&
        (!m_columnTransparencyPopup || m_columnTransparencyPopup->isHidden())) {
      column->setCamstandVisible(!column->isCamstandVisible());
      app->getCurrentXsheet()->notifyXsheetSoundChanged();
    } else if (m_doOnRelease == TogglePreviewVisible)
      column->setPreviewVisible(!column->isPreviewVisible());
    else if (m_doOnRelease == ToggleLock)
      column->lock(!column->isLocked());
    else
      assert(false);

    app->getCurrentScene()->notifySceneChanged();
    app->getCurrentXsheet()->notifyXsheetChanged();
    update();
    m_doOnRelease = 0;
  }

  if (m_transparencyPopupTimer) m_transparencyPopupTimer->stop();

  m_viewer->setQtModifiers(0);
  m_viewer->dragToolRelease(event);
  m_isPanning = false;
  m_viewer->stopAutoPan();
}

//-----------------------------------------------------------------------------

void ColumnArea::mouseDoubleClickEvent(QMouseEvent *event) {
  const Orientation *o = m_viewer->orientation ();

  QPoint pos = event->pos();
  int col    = m_viewer->xyToPosition (pos).layer ();
  CellPosition cellPosition (0, col);
  QPoint topLeft = m_viewer->positionToXY (cellPosition);
  QPoint mouseInCell = pos - topLeft;

#ifdef LINETEST
  // Camera column
  if (col == -1) return;
#endif

  if (!o->rect (PredefinedRect::COLUMN_NAME).contains(mouseInCell))
    return;

  TXsheet *xsh = m_viewer->getXsheet();
  if (col >= 0 && xsh->isColumnEmpty(col)) return;

  QRect renameRect = o->rect (PredefinedRect::RENAME_COLUMN).translated (topLeft);
  m_renameColumnField->show(renameRect, col);
}

//-----------------------------------------------------------------------------

void ColumnArea::contextMenuEvent(QContextMenuEvent *event) {
#ifndef _WIN32
  /* On windows the widget receive the release event before the menu
     is shown, on linux and osx the release event is lost, never
     received by the widget */
  QMouseEvent fakeRelease(QEvent::MouseButtonRelease, event->pos(),
                          Qt::RightButton, Qt::NoButton, Qt::NoModifier);

  QApplication::instance()->sendEvent(this, &fakeRelease);
#endif
  const Orientation *o = m_viewer->orientation ();

  int col = m_viewer->xyToPosition (event->pos ()).layer ();
  if (col < 0)  // CAMERA
    return;
  m_viewer->setCurrentColumn(col);
  TXsheet *xsh = m_viewer->getXsheet();
  QPoint topLeft = m_viewer->positionToXY (CellPosition (0, col));
  QPoint mouseInCell = event->pos () - topLeft;

  QMenu menu(this);
  CommandManager *cmdManager = CommandManager::instance();

  //---- Preview
  if (!xsh->isColumnEmpty(col) &&
      o->rect (PredefinedRect::EYE_AREA).contains(mouseInCell)) {
    menu.setObjectName("xsheetColumnAreaMenu_Preview");

    menu.addAction(cmdManager->getAction("MI_EnableThisColumnOnly"));
    menu.addAction(cmdManager->getAction("MI_EnableSelectedColumns"));
    menu.addAction(cmdManager->getAction("MI_EnableAllColumns"));
    menu.addAction(cmdManager->getAction("MI_DisableAllColumns"));
    menu.addAction(cmdManager->getAction("MI_DisableSelectedColumns"));
    menu.addAction(cmdManager->getAction("MI_SwapEnabledColumns"));
  }
  //---- Lock
  else if (!xsh->isColumnEmpty(col) &&
           o->rect (PredefinedRect::LOCK).contains(mouseInCell)) {
    menu.setObjectName("xsheetColumnAreaMenu_Lock");

    menu.addAction(cmdManager->getAction("MI_LockThisColumnOnly"));
    menu.addAction(cmdManager->getAction("MI_LockSelectedColumns"));
    menu.addAction(cmdManager->getAction("MI_LockAllColumns"));
    menu.addAction(cmdManager->getAction("MI_UnlockSelectedColumns"));
    menu.addAction(cmdManager->getAction("MI_UnlockAllColumns"));
    menu.addAction(cmdManager->getAction("MI_ToggleColumnLocks"));
  }
  //---- Camstand
  else if (!xsh->isColumnEmpty(col) &&
           o->rect (PredefinedRect::PREVIEW_LAYER_AREA).contains(mouseInCell)) {
    menu.setObjectName("xsheetColumnAreaMenu_Camstand");

    menu.addAction(cmdManager->getAction("MI_ActivateThisColumnOnly"));
    menu.addAction(cmdManager->getAction("MI_ActivateSelectedColumns"));
    menu.addAction(cmdManager->getAction("MI_ActivateAllColumns"));
    menu.addAction(cmdManager->getAction("MI_DeactivateAllColumns"));
    menu.addAction(cmdManager->getAction("MI_DeactivateSelectedColumns"));
    menu.addAction(cmdManager->getAction("MI_ToggleColumnsActivation"));
    // hide all columns placed on the left
    menu.addAction(cmdManager->getAction("MI_DeactivateUpperColumns"));
  }
  // right clicking another area / right clicking empty column head
  else {
    int r0, r1;
    xsh->getCellRange(col, r0, r1);
    TXshCell cell = xsh->getCell(r0, col);
    menu.addAction(cmdManager->getAction(MI_Cut));
    menu.addAction(cmdManager->getAction(MI_Copy));
    menu.addAction(cmdManager->getAction(MI_Paste));
    menu.addAction(cmdManager->getAction(MI_Clear));
    menu.addAction(cmdManager->getAction(MI_Insert));
    menu.addSeparator();
    menu.addAction(cmdManager->getAction(MI_InsertFx));
    menu.addSeparator();
    if (m_viewer->getXsheet()->isColumnEmpty(col) ||
        (cell.m_level && cell.m_level->getChildLevel()))
      menu.addAction(cmdManager->getAction(MI_OpenChild));

    // Close sub xsheet and move to parent sheet
    ToonzScene *scene      = TApp::instance()->getCurrentScene()->getScene();
    ChildStack *childStack = scene->getChildStack();
    if (childStack && childStack->getAncestorCount() > 0) {
      menu.addAction(cmdManager->getAction(MI_CloseChild));
    }

    menu.addAction(cmdManager->getAction(MI_Collapse));
    if (cell.m_level && cell.m_level->getChildLevel()) {
      menu.addAction(cmdManager->getAction(MI_Resequence));
      menu.addAction(cmdManager->getAction(MI_CloneChild));
      menu.addAction(cmdManager->getAction(MI_ExplodeChild));
    }
    menu.addSeparator();
    menu.addAction(cmdManager->getAction(MI_FoldColumns));

    // force the selected cells placed in n-steps
    if (!xsh->isColumnEmpty(col)) {
      menu.addSeparator();
      QMenu *reframeSubMenu = new QMenu(tr("Reframe"), this);
      {
        reframeSubMenu->addAction(cmdManager->getAction(MI_Reframe1));
        reframeSubMenu->addAction(cmdManager->getAction(MI_Reframe2));
        reframeSubMenu->addAction(cmdManager->getAction(MI_Reframe3));
        reframeSubMenu->addAction(cmdManager->getAction(MI_Reframe4));
      }
      menu.addMenu(reframeSubMenu);
    }

    if (containsRasterLevel(m_viewer->getColumnSelection())) {
      QMenu *subsampleSubMenu = new QMenu(tr("Subsampling"), this);
      {
        subsampleSubMenu->addAction(m_subsampling1);
        subsampleSubMenu->addAction(m_subsampling2);
        subsampleSubMenu->addAction(m_subsampling3);
        subsampleSubMenu->addAction(m_subsampling4);
      }
      menu.addMenu(subsampleSubMenu);
    }

    if (!xsh->isColumnEmpty(col)) {
      menu.addAction(cmdManager->getAction(MI_ReplaceLevel));
      menu.addAction(cmdManager->getAction(MI_ReplaceParentDirectory));
    }
  }

  menu.exec(event->globalPos());
}

//-----------------------------------------------------------------------------

void ColumnArea::onSubSampling(QAction *action) {
  int subsampling;
  if (action == m_subsampling1)
    subsampling = 1;
  else if (action == m_subsampling2)
    subsampling = 2;
  else if (action == m_subsampling3)
    subsampling = 3;
  else if (action == m_subsampling4)
    subsampling = 4;

  TColumnSelection *selection = m_viewer->getColumnSelection();
  TXsheet *xsh                = m_viewer->getXsheet();
  assert(selection && xsh);
  const set<int> indexes = selection->getIndices();
  set<int>::const_iterator it;
  for (it = indexes.begin(); it != indexes.end(); it++) {
    TXshColumn *column          = xsh->getColumn(*it);
    TXshColumn::ColumnType type = column->getColumnType();
    if (type != TXshColumn::eLevelType) continue;
    const QSet<TXshSimpleLevel *> levels = getLevels(column);
    QSet<TXshSimpleLevel *>::const_iterator it2;
    for (it2 = levels.begin(); it2 != levels.end(); it2++) {
      TXshSimpleLevel *sl = *it2;
      if (sl->getProperties()->getDirtyFlag()) continue;
      int type = sl->getType();
      if (type == TZI_XSHLEVEL || type == TZP_XSHLEVEL ||
          type == OVL_XSHLEVEL) {
        sl->getProperties()->setSubsampling(subsampling);
        sl->invalidateFrames();
      }
    }
  }
  TApp::instance()
      ->getCurrentXsheet()
      ->getXsheet()
      ->getStageObjectTree()
      ->invalidateAll();
  TApp::instance()->getCurrentScene()->notifySceneChanged();
}

}  // namespace XsheetGUI
