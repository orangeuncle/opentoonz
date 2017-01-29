

#include "xsheetviewer.h"
#include "sceneviewerevents.h"
#include "tapp.h"
#include "floatingpanelcommand.h"
#include "menubarcommandids.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/tfxhandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/txshpalettelevel.h"
#include "toonz/preferences.h"
#include "toonz/sceneproperties.h"
#include "toonzqt/tselectionhandle.h"
#include "toonzqt/icongenerator.h"
#include "cellselection.h"
#include "keyframeselection.h"
#include "cellkeyframeselection.h"
#include "columnselection.h"
#include "xsheetdragtool.h"
#include "toonzqt/gutil.h"

#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshzeraryfxcolumn.h"
#include "toonz/toonzscene.h"
#include "toonz/columnfan.h"
#include "toonz/txshnoteset.h"
#include "toonz/childstack.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/preferences.h"
#include "tconvert.h"

#include "tenv.h"

#include <QPainter>
#include <QScrollBar>
#include <QMouseEvent>

TEnv::IntVar FrameDisplayStyleInXsheetRowArea(
    "FrameDisplayStyleInXsheetRowArea", 0);

//=============================================================================
namespace XsheetGUI {
//-----------------------------------------------------------------------------

const int ColumnWidth = 74;
const int RowHeight   = 20;
const int SCROLLBAR_WIDTH = 16;

}  // namespace XsheetGUI

//=============================================================================
// XsheetViewer
//-----------------------------------------------------------------------------

void XsheetViewer::getCellTypeAndColors(int &ltype, QColor &cellColor,
                                        QColor &sideColor, const TXshCell &cell,
                                        bool isSelected) {
  if (cell.isEmpty())
    ltype = NO_XSHLEVEL;
  else {
    ltype = cell.m_level->getType();
    switch (ltype) {
    case TZI_XSHLEVEL:
    case OVL_XSHLEVEL:
      cellColor = (isSelected) ? getSelectedFullcolorColumnColor()
                               : getFullcolorColumnColor();
      sideColor = getFullcolorColumnBorderColor();
      break;
    case PLI_XSHLEVEL:
      cellColor = (isSelected) ? getSelectedVectorColumnColor()
                               : getVectorColumnColor();
      sideColor = getVectorColumnBorderColor();
      break;
    case TZP_XSHLEVEL:
      cellColor =
          (isSelected) ? getSelectedLevelColumnColor() : getLevelColumnColor();
      sideColor = getLevelColumnBorderColor();
      break;
    case ZERARYFX_XSHLEVEL:
      cellColor =
          (isSelected) ? getSelectedFxColumnColor() : getFxColumnColor();
      sideColor = getFxColumnBorderColor();
      break;
    case CHILD_XSHLEVEL:
      cellColor =
          (isSelected) ? getSelectedChildColumnColor() : getChildColumnColor();
      sideColor = getChildColumnBorderColor();
      break;
    case SND_XSHLEVEL:
      cellColor =
          (isSelected) ? m_selectedSoundColumnColor : m_soundColumnColor;
      sideColor = m_soundColumnBorderColor;
      break;
    case SND_TXT_XSHLEVEL:
      cellColor = XsheetGUI::SoundTextColumnColor;
      sideColor = XsheetGUI::SoundTextColumnBorderColor;
      break;
    case MESH_XSHLEVEL:
      cellColor =
          (isSelected) ? getSelectedMeshColumnColor() : getMeshColumnColor();
      sideColor = getMeshColumnBorderColor();
      break;
    case UNKNOWN_XSHLEVEL:
    case NO_XSHLEVEL:
    default:
      // non dovrebbe succedere
      cellColor = grey210;
      sideColor = grey150;
    }
  }
}

//-----------------------------------------------------------------------------

void XsheetViewer::getColumnColor(QColor &color, QColor &sideColor, int index,
                                  TXsheet *xsh) {
  if (index < 0 || xsh->isColumnEmpty(index)) return;
  int r0, r1;
  xsh->getCellRange(index, r0, r1);
  if (0 <= r0 && r0 <= r1) {
    // column color depends on the level type in the top-most occupied cell
    TXshCell cell = xsh->getCell(r0, index);
    int ltype;
    getCellTypeAndColors(ltype, color, sideColor, cell);
  }
  if (xsh->getColumn(index)->isMask()) color = QColor(255, 0, 255);
}

//-----------------------------------------------------------------------------

#if QT_VERSION >= 0x050500
XsheetViewer::XsheetViewer(QWidget *parent, Qt::WindowFlags flags)
#else
XsheetViewer::XsheetViewer(QWidget *parent, Qt::WFlags flags)
#endif
    : QFrame(parent)
    , m_timerId(0)
    , m_autoPanSpeed(0, 0)
    , m_dragTool(0)
    , m_columnSelection(new TColumnSelection())
    , m_cellKeyframeSelection(new TCellKeyframeSelection(
          new TCellSelection(), new TKeyframeSelection()))
    , m_scrubCol(-1)
    , m_scrubRow0(-1)
    , m_scrubRow1(-1)
    , m_isCurrentFrameSwitched(false)
    , m_isCurrentColumnSwitched(false)
    , m_isComputingSize(false)
    , m_currentNoteIndex(0)
    , m_qtModifiers(0)
    , m_frameDisplayStyle(to_enum(FrameDisplayStyleInXsheetRowArea))
    , m_orientation (nullptr) {

	setFocusPolicy(Qt::StrongFocus);

  setFrameStyle(QFrame::StyledPanel);
  setObjectName("XsheetViewer");

  m_orientation = orientations.leftToRight ();

  m_cellKeyframeSelection->setXsheetHandle(
      TApp::instance()->getCurrentXsheet());

  QRect noteArea (0, 0, 75, 120);
  m_noteArea = new XsheetGUI::NoteArea(this);
  m_noteScrollArea = new XsheetScrollArea(this);
  m_noteScrollArea->setWidget(m_noteArea);
  m_noteScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_noteScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_cellArea       = new XsheetGUI::CellArea(this);
  m_cellScrollArea = new XsheetScrollArea(this);
  m_cellScrollArea->setObjectName("xsheetArea");
  m_cellScrollArea->setWidget(m_cellArea);
  m_cellScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  m_cellScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  m_columnArea       = new XsheetGUI::ColumnArea(this);
  m_columnScrollArea = new XsheetScrollArea(this);
  m_columnScrollArea->setObjectName("xsheetArea");
  m_columnScrollArea->setWidget(m_columnArea);
  m_columnScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_columnScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_rowArea       = new XsheetGUI::RowArea(this);
  m_rowScrollArea = new XsheetScrollArea(this);
  m_rowScrollArea->setObjectName("xsheetArea");
  m_rowScrollArea->setWidget(m_rowArea);
  m_rowScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_rowScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  connectScrollBars ();

  connect (this, &XsheetViewer::orientationChanged, this, &XsheetViewer::onOrientationChanged);

  emit orientationChanged (orientation ());
}

//-----------------------------------------------------------------------------

XsheetViewer::~XsheetViewer() {
  delete m_cellKeyframeSelection;
  delete m_dragTool;
}

//-----------------------------------------------------------------------------

void XsheetViewer::setDragTool(XsheetGUI::DragTool *dragTool) {
  assert(m_dragTool == 0);
  m_dragTool = dragTool;
}

//-----------------------------------------------------------------------------

void XsheetViewer::dragToolClick(QMouseEvent *e) {
  if (m_dragTool) m_dragTool->onClick(e);
}

void XsheetViewer::dragToolDrag(QMouseEvent *e) {
  if (m_dragTool) m_dragTool->onDrag(e);
}

void XsheetViewer::dragToolRelease(QMouseEvent *e) {
  if (m_dragTool) {
    m_dragTool->onRelease(e);
    delete getDragTool();
    m_dragTool = 0;
  }
}

//-----------------------------------------------------------------------------

void XsheetViewer::dragToolClick(QDropEvent *e) {
  if (m_dragTool) m_dragTool->onClick(e);
}

void XsheetViewer::dragToolDrag(QDropEvent *e) {
  if (m_dragTool) m_dragTool->onDrag(e);
}

void XsheetViewer::dragToolRelease(QDropEvent *e) {
  if (m_dragTool) {
    m_dragTool->onRelease(e);
    delete getDragTool();
    m_dragTool = 0;
  }
}

void XsheetViewer::dragToolLeave(QEvent *e) {
  if (m_dragTool) {
    delete getDragTool();
    m_dragTool = 0;
  }
}

//-----------------------------------------------------------------------------

const Orientation *XsheetViewer::orientation () const {
  if (!m_orientation)
    throw std::exception ("!m_orientation");
  return m_orientation;
}

void XsheetViewer::flipOrientation () {
  m_orientation = orientation ()->next ();
  emit orientationChanged (orientation ());
}

void XsheetViewer::onOrientationChanged (const Orientation *newOrientation) {
  disconnectScrollBars ();

  positionSections ();
  refreshContentSize (0, 0);

  connectScrollBars ();

  update ();
}

void XsheetViewer::positionSections () {
  const Orientation *o = orientation ();
  QRect size = QRect (QPoint (0, 0), geometry ().size ());
  NumberRange allLayer = o->layerSide (size);
  NumberRange allFrame = o->frameSide (size);

  NumberRange headerLayer = o->range (PredefinedRange::HEADER_LAYER);
  NumberRange headerFrame = o->range (PredefinedRange::HEADER_FRAME);
  NumberRange bodyLayer (headerLayer.to (), allLayer.to ());
  NumberRange bodyFrame (headerFrame.to (), allFrame.to ());

  m_noteScrollArea->setGeometry (o->frameLayerRect (headerFrame, headerLayer));
  m_cellScrollArea->setGeometry (o->frameLayerRect (bodyFrame, bodyLayer));
  m_columnScrollArea->setGeometry (o->frameLayerRect (
    headerFrame, bodyLayer.adjusted (0, -XsheetGUI::SCROLLBAR_WIDTH)));
  m_rowScrollArea->setGeometry (o->frameLayerRect (
    bodyFrame.adjusted (0, -XsheetGUI::SCROLLBAR_WIDTH), headerLayer));
}

void XsheetViewer::disconnectScrollBars () {
  connectOrDisconnectScrollBars (false);
}
void XsheetViewer::connectScrollBars () {
  connectOrDisconnectScrollBars (true);
}

void XsheetViewer::connectOrDisconnectScrollBars (bool toConnect) {
  const Orientation *o = orientation ();
  bool isVertical = o->isVerticalTimeline ();
  QWidget *scrolledVertically =
    (isVertical ? m_rowScrollArea : m_columnScrollArea)->verticalScrollBar ();
  QWidget *scrolledHorizontally =
    (isVertical ? m_columnScrollArea : m_rowScrollArea)->horizontalScrollBar ();

  connectOrDisconnect (toConnect,
    scrolledVertically, SIGNAL (valueChanged (int)),
    m_cellScrollArea->verticalScrollBar (), SLOT (setValue (int)));
  connectOrDisconnect (toConnect,
    m_cellScrollArea->verticalScrollBar (), SIGNAL (valueChanged (int)),
    scrolledVertically, SLOT (setValue (int)));

  connectOrDisconnect (toConnect,
    scrolledHorizontally, SIGNAL (valueChanged (int)),
    m_cellScrollArea->horizontalScrollBar (), SLOT (setValue (int)));
  connectOrDisconnect (toConnect,
    m_cellScrollArea->horizontalScrollBar (), SIGNAL (valueChanged (int)),
    scrolledHorizontally, SLOT (setValue (int)));

  connectOrDisconnect (toConnect,
    m_cellScrollArea->verticalScrollBar (), SIGNAL (valueChanged (int)),
    this, isVertical ? SLOT (updateCellRowAree ()) : SLOT (updateCellColumnAree ()));
  connectOrDisconnect (toConnect,
    m_cellScrollArea->horizontalScrollBar (), SIGNAL (valueChanged (int)),
    this, isVertical ? SLOT (updateCellColumnAree ()) : SLOT (updateCellRowAree ()));
}

void XsheetViewer::connectOrDisconnect (bool toConnect,
  QWidget *sender, const char *signal, QWidget *receiver, const char *slot) {
  if (toConnect)
    connect (sender, signal, receiver, slot);
  else
    disconnect (sender, signal, receiver, slot);
}

//-----------------------------------------------------------------------------

TXsheet *XsheetViewer::getXsheet() const {
  return TApp::instance()->getCurrentXsheet()->getXsheet();
}

//-----------------------------------------------------------------------------

int XsheetViewer::getCurrentColumn() const {
  return TApp::instance()->getCurrentColumn()->getColumnIndex();
}

//-----------------------------------------------------------------------------

int XsheetViewer::getCurrentRow() const {
  return TApp::instance()->getCurrentFrame()->getFrame();
}

//-----------------------------------------------------------------------------

TStageObjectId XsheetViewer::getObjectId(int col) const {
  if (col < 0) return TStageObjectId::CameraId(0);
  return TStageObjectId::ColumnId(col);
}
//-----------------------------------------------------------------------------

void XsheetViewer::setCurrentColumn(int col) {
  TColumnHandle *columnHandle = TApp::instance()->getCurrentColumn();
  if (col != columnHandle->getColumnIndex()) {
    columnHandle->setColumnIndex(col);
    // E' necessario per il caso in cui si passa da colonna di camera a altra
    // colonna
    // o nel caso in cui si passa da una spline a una colonna.
    TObjectHandle *objectHandle = TApp::instance()->getCurrentObject();
    if (col >= 0 && objectHandle->isSpline()) objectHandle->setIsSpline(false);
    updateCellColumnAree();
    if (col >= 0) {
      objectHandle->setObjectId(TStageObjectId::ColumnId(col));
      TXsheet *xsh       = getXsheet();
      TXshColumn *column = xsh->getColumn(col);
      if (!column || column->isEmpty())
        TApp::instance()->getCurrentFx()->setFx(0);
      else
        TApp::instance()->getCurrentFx()->setFx(column->getFx());
    }
    return;
  }
}

//-----------------------------------------------------------------------------

void XsheetViewer::setCurrentRow(int row) {
  // POTREBBE NON ESSER PIU' NECESSARIO CON LE NUOVE MODIFICHE PER CLEANUP A
  // COLORI
  /*TFrameHandle* frameHandle = TApp::instance()->getCurrentFrame();
if(row == frameHandle->getFrameIndex() && frameHandle->getFrameType() ==
TFrameHandle::SceneFrame)
{
//E' necessario per il caso in cui la paletta corrente e' la paletta di cleanup.
TPaletteHandle* levelPaletteHandle =
TApp::instance()->getPaletteController()->getCurrentLevelPalette();
TXshLevel *xl = TApp::instance()->getCurrentLevel()->getLevel();
if(xl && xl->getSimpleLevel())
                  levelPaletteHandle->setPalette(xl->getSimpleLevel()->getPalette());
else if(xl && xl->getPaletteLevel())
                  levelPaletteHandle->setPalette(xl->getPaletteLevel()->getPalette());
else
levelPaletteHandle->setPalette(0);
return;
}
frameHandle->setFrame(row);*/
  TApp::instance()->getCurrentFrame()->setFrame(row);
}

//-----------------------------------------------------------------------------

void XsheetViewer::scroll(QPoint delta) {
  int x = delta.x();
  int y = delta.y();
  prepareToScroll(y);

  int valueH    = m_cellScrollArea->horizontalScrollBar()->value() + x;
  int valueV    = m_cellScrollArea->verticalScrollBar()->value() + y;
  int maxValueH = m_cellScrollArea->horizontalScrollBar()->maximum();
  int maxValueV = m_cellScrollArea->verticalScrollBar()->maximum();

  bool notUpdateSizeH = maxValueH > valueH && x >= 0;
  bool notUpdateSizeV = maxValueV > valueV && y >= 0;
  if (!notUpdateSizeH && !notUpdateSizeV)  // Resize orizzontale e verticale
    refreshContentSize(x, y);
  else if (notUpdateSizeH && !notUpdateSizeV)  // Resize verticale
    refreshContentSize(0, y);
  else if (!notUpdateSizeH && notUpdateSizeV)  // Resize orizzontale
    refreshContentSize(x, 0);

  if (valueH > maxValueH && x > 0)  // Se il valore e' maggiore del max e x>0
                                    // scrollo al massimo valore orizzontale
    valueH = m_cellScrollArea->horizontalScrollBar()->maximum();

  if (valueV > maxValueV && y > 0)  // Se il valore e' maggiore del max e y>0
                                    // scrollo al massimo valore verticale
    valueV = m_cellScrollArea->verticalScrollBar()->maximum();

  m_cellScrollArea->horizontalScrollBar()->setValue(valueH);
  m_cellScrollArea->verticalScrollBar()->setValue(valueV);
}

//-----------------------------------------------------------------------------

void XsheetViewer::onPrepareToScroll(int dy) { refreshContentSize(0, dy); }

//-----------------------------------------------------------------------------

void XsheetViewer::setAutoPanSpeed(const QPoint &speed) {
  bool wasAutoPanning = isAutoPanning();
  m_autoPanSpeed      = speed;
  if (isAutoPanning() && !wasAutoPanning && m_timerId == 0)
    m_timerId = startTimer(40);
  else if (!isAutoPanning() && wasAutoPanning && m_timerId != 0) {
    killTimer(m_timerId);
    m_timerId = 0;
  }
}

//-----------------------------------------------------------------------------

static int getAutoPanSpeed(int pixels) {
  int f = 40;
  return std::min(100, (f - 1 + pixels * f) / 100);
}

//-----------------------------------------------------------------------------

void XsheetViewer::setAutoPanSpeed(const QRect &widgetBounds,
                                   const QPoint &mousePos) {
  QPoint speed;
  int limit = 100, factor = 30;
  if (mousePos.x() < widgetBounds.left())
    speed.setX(-getAutoPanSpeed(widgetBounds.left() - mousePos.x()));
  else if (mousePos.x() > widgetBounds.right())
    speed.setX(getAutoPanSpeed(mousePos.x() - widgetBounds.right()));
  if (mousePos.y() < widgetBounds.top())
    speed.setY(-getAutoPanSpeed(widgetBounds.top() - mousePos.y()));
  else if (mousePos.y() > widgetBounds.bottom())
    speed.setY(getAutoPanSpeed(mousePos.y() - widgetBounds.bottom()));
  setAutoPanSpeed(speed);
  m_lastAutoPanPos = mousePos;
}

//-----------------------------------------------------------------------------

void XsheetViewer::timerEvent(QTimerEvent *) {
  if (!isAutoPanning()) return;
  scroll(m_autoPanSpeed);
  if (!m_dragTool) return;
  QMouseEvent mouseEvent(QEvent::MouseMove, m_lastAutoPanPos - m_autoPanSpeed,
                         Qt::NoButton, 0, m_qtModifiers);
  m_dragTool->onDrag(&mouseEvent);
  m_lastAutoPanPos += m_autoPanSpeed;
}

//-----------------------------------------------------------------------------

// adjust sizes after scrolling event
bool XsheetViewer::refreshContentSize(int dx, int dy) {
  QSize viewportSize = m_cellScrollArea->viewport()->size();
  QPoint offset      = m_cellArea->pos();
  offset = QPoint(qMin(0, offset.x() - dx), qMin(0, offset.y() - dy)); // what?

  TXsheet *xsh    = getXsheet();
  int frameCount  = xsh ? xsh->getFrameCount() : 0;
  int columnCount = xsh ? xsh->getColumnCount() : 0;
  QPoint contentSize = positionToXY (CellPosition (frameCount + 1, columnCount + 1));

  QSize actualSize(contentSize.x (), contentSize.y ());
  int x = viewportSize.width () - offset.x (); // wtf is going on
  int y = viewportSize.height() - offset.y();
  if (x > actualSize.width()) actualSize.setWidth(x);
  if (y > actualSize.height()) actualSize.setHeight(y);

  if (actualSize == m_cellArea->size())
   return false;
  else {
    const Orientation *o = orientation ();
    NumberRange allLayer = o->layerSide (QRect (QPoint (0, 0), actualSize));
    NumberRange allFrame = o->frameSide (QRect (QPoint (0, 0), actualSize));
    NumberRange headerLayer = o->range (PredefinedRange::HEADER_LAYER);
    NumberRange headerFrame = o->range (PredefinedRange::HEADER_FRAME);

    m_isComputingSize = true;
    m_noteArea->setFixedSize (o->rect (PredefinedRect::NOTE_AREA).size ());
    m_cellArea->setFixedSize(actualSize);
    m_rowArea->setFixedSize(o->frameLayerRect (allFrame, headerLayer).size ());
    m_columnArea->setFixedSize(o->frameLayerRect (headerFrame, allLayer).size ());
    m_isComputingSize = false;
    return true;
  }
}

//-----------------------------------------------------------------------------

// call when in doubt
void XsheetViewer::updateAreeSize() {
  const Orientation *o = orientation ();
  QRect viewArea (QPoint (0, 0), m_cellScrollArea->geometry ()
    .adjusted (0, 0, -XsheetGUI::SCROLLBAR_WIDTH, -XsheetGUI::SCROLLBAR_WIDTH).size ());

  QPoint areaFilled (0, 0);
  TXsheet *xsh = getXsheet ();
  if (xsh)
    areaFilled = positionToXY (CellPosition (xsh->getFrameCount () + 1, xsh->getColumnCount () + 1));
  viewArea = viewArea & QRect (0, 0, areaFilled.x (), areaFilled.y ());

  NumberRange allLayer = o->layerSide (viewArea);
  NumberRange allFrame = o->frameSide (viewArea);
  NumberRange headerLayer = o->range (PredefinedRange::HEADER_LAYER);
  NumberRange headerFrame = o->range (PredefinedRange::HEADER_FRAME);

  m_cellArea->setFixedSize (viewArea.size ());
  m_rowArea->setFixedSize (o->frameLayerRect (allFrame, headerLayer).size ());
  m_columnArea->setFixedSize (o->frameLayerRect (headerFrame, allLayer).size ());
}

//-----------------------------------------------------------------------------

CellPosition XsheetViewer::xyToPosition (const QPoint &point) const {
  const Orientation *o = orientation ();
	return o->xyToPosition (point, getXsheet ()->getColumnFan (o));
}
CellPosition XsheetViewer::xyToPosition (const TPoint &point) const {
	return xyToPosition (QPoint (point.x, point.y));
}
CellPosition XsheetViewer::xyToPosition (const TPointD &point) const {
	return xyToPosition (QPoint ((int) point.x, (int) point.y));
}

//-----------------------------------------------------------------------------

QPoint XsheetViewer::positionToXY (const CellPosition &pos) const {
  const Orientation *o = orientation ();
  return o->positionToXY (pos, getXsheet ()->getColumnFan (o));
}

int XsheetViewer::columnToLayerAxis (int layer) const {
  const Orientation *o = orientation ();
  return o->colToLayerAxis (layer, getXsheet ()->getColumnFan (o));
}
int XsheetViewer::rowToFrameAxis (int frame) const {
	return orientation ()->rowToFrameAxis (frame);
}

//-----------------------------------------------------------------------------

CellRange XsheetViewer::xyRectToRange (const QRect &rect) const {
	CellPosition topLeft = xyToPosition (rect.topLeft ());
	CellPosition bottomRight = xyToPosition (rect.bottomRight ());
	return CellRange (topLeft, bottomRight);
}

//-----------------------------------------------------------------------------

QRect XsheetViewer::rangeToXYRect (const CellRange &range) const {
	QPoint from = positionToXY (range.from ());
	QPoint to = positionToXY (range.to ());
	QPoint topLeft = QPoint (min (from.x (), to.x ()), min (from.y (), to.y ()));
	QPoint bottomRight = QPoint (max (from.x (), to.x ()), max (from.y (), to.y ()));
	return QRect (topLeft, bottomRight);
}

//-----------------------------------------------------------------------------

void XsheetViewer::drawPredefinedPath (QPainter &p, PredefinedPath which, const CellPosition &pos,
  optional<QColor> fill, optional<QColor> outline) const {
  QPoint xy = positionToXY (pos);
  QPainterPath path = orientation ()->path (which).translated (xy);
  if (fill)
    p.fillPath (path, QBrush (*fill));
  if (outline) {
    p.setPen (*outline);
    p.drawPath (path);
  }
}

//-----------------------------------------------------------------------------

bool XsheetViewer::areCellsSelectedEmpty() {
  int r0, c0, r1, c1;
  getCellSelection()->getSelectedCells(r0, c0, r1, c1);
  int i, j;
  for (i = r0; i <= r1; i++)
    for (j = c0; j <= c1; j++)
      if (!getXsheet()->getCell(i, j).isEmpty()) return false;
  return true;
}

//-----------------------------------------------------------------------------

bool XsheetViewer::areSoundCellsSelected() {
  int r0, c0, r1, c1;
  getCellSelection()->getSelectedCells(r0, c0, r1, c1);
  int i, j;
  for (i = r0; i <= r1; i++)
    for (j = c0; j <= c1; j++) {
      TXshCell cell = getXsheet()->getCell(i, j);
      if (cell.isEmpty() || cell.getSoundLevel()) continue;
      return false;
    }
  return !areCellsSelectedEmpty();
}

//-----------------------------------------------------------------------------

bool XsheetViewer::areSoundTextCellsSelected() {
  int r0, c0, r1, c1;
  getCellSelection()->getSelectedCells(r0, c0, r1, c1);
  int i, j;
  for (i = r0; i <= r1; i++)
    for (j = c0; j <= c1; j++) {
      TXshCell cell = getXsheet()->getCell(i, j);
      if (cell.isEmpty() || cell.getSoundTextLevel()) continue;
      return false;
    }
  return !areCellsSelectedEmpty();
}

//-----------------------------------------------------------------------------

void XsheetViewer::setScrubHighlight(int row, int startRow, int col) {
  if (m_scrubCol == -1) m_scrubCol = col;
  m_scrubRow0                      = std::min(row, startRow);
  m_scrubRow1                      = std::max(row, startRow);
  return;
}

//-----------------------------------------------------------------------------

void XsheetViewer::resetScrubHighlight() {
  m_scrubCol = m_scrubRow0 = m_scrubRow1 = -1;
  return;
}

//-----------------------------------------------------------------------------

void XsheetViewer::getScrubHeighlight(int &R0, int &R1) {
  R0 = m_scrubRow0;
  R1 = m_scrubRow1;
  return;
}

//-----------------------------------------------------------------------------

bool XsheetViewer::isScrubHighlighted(int row, int col) {
  if (m_scrubCol == col && m_scrubRow0 <= row && row <= m_scrubRow1)
    return true;
  return false;
}

//-----------------------------------------------------------------------------

void XsheetViewer::showEvent(QShowEvent *) {
  registerFrameScroller();
  if (m_isCurrentFrameSwitched) onCurrentFrameSwitched();
  if (m_isCurrentColumnSwitched) onCurrentColumnSwitched();
  m_isCurrentFrameSwitched = false;

  TApp *app = TApp::instance();

  bool ret = connect(app->getCurrentColumn(), SIGNAL(columnIndexSwitched()),
                     this, SLOT(onCurrentColumnSwitched()));
  ret = ret && connect(app->getCurrentFrame(), SIGNAL(frameSwitched()), this,
                       SLOT(onCurrentFrameSwitched()));
  ret = ret && connect(app->getCurrentFrame(), SIGNAL(isPlayingStatusChanged()),
                       this, SLOT(onPlayingStatusChanged()));
  ret = ret && connect(app->getCurrentFrame(), SIGNAL(scrubStopped()), this,
                       SLOT(onScrubStopped()));

  ret = ret && connect(app->getCurrentObject(), SIGNAL(objectChanged(bool)),
                       this, SLOT(updateAllAree(bool)));

  TSceneHandle *sceneHandle = app->getCurrentScene();
  ret = ret && connect(sceneHandle, SIGNAL(sceneSwitched()), this,
                       SLOT(onSceneSwitched()));
  ret = ret && connect(sceneHandle, SIGNAL(nameSceneChanged()), this,
                       SLOT(changeWindowTitle()));

  TXsheetHandle *xsheetHandle = app->getCurrentXsheet();
  ret = ret && connect(xsheetHandle, SIGNAL(xsheetSwitched()), this,
                       SLOT(updateAllAree()));
  ret = ret && connect(xsheetHandle, SIGNAL(xsheetSwitched()), this,
                       SLOT(resetXsheetNotes()));
  ret = ret && connect(xsheetHandle, SIGNAL(xsheetChanged()), this,
                       SLOT(onXsheetChanged()));
  ret = ret && connect(xsheetHandle, SIGNAL(xsheetChanged()), this,
                       SLOT(changeWindowTitle()));

  ret = ret &&
        connect(app->getCurrentSelection(),
                SIGNAL(selectionSwitched(TSelection *, TSelection *)), this,
                SLOT(onSelectionSwitched(TSelection *, TSelection *)));
  // update titlebar when the cell selection region is changed
  ret = ret && connect(app->getCurrentSelection(),
                       SIGNAL(selectionChanged(TSelection *)), this,
                       SLOT(onSelectionChanged(TSelection *)));
  // show the current level name to title bar
  ret = ret &&
        connect(app->getCurrentLevel(), SIGNAL(xshLevelSwitched(TXshLevel *)),
                this, SLOT(changeWindowTitle()));

  ret = ret && connect(IconGenerator::instance(), SIGNAL(iconGenerated()), this,
                       SLOT(updateColumnArea()));

  assert(ret);
  refreshContentSize(0, 0);
  changeWindowTitle();
}

//-----------------------------------------------------------------------------

void XsheetViewer::hideEvent(QHideEvent *) {
  unregisterFrameScroller();

  TApp *app = TApp::instance();

  disconnect(app->getCurrentColumn(), SIGNAL(columnIndexSwitched()), this,
             SLOT(onCurrentColumnSwitched()));
  disconnect(app->getCurrentFrame(), SIGNAL(frameSwitched()), this,
             SLOT(onCurrentFrameSwitched()));
  disconnect(app->getCurrentFrame(), SIGNAL(scrubStopped()), this,
             SLOT(onScrubStopped()));

  disconnect(app->getCurrentObject(), SIGNAL(objectChanged(bool)), this,
             SLOT(updateAllAree(bool)));

  TSceneHandle *sceneHandle = app->getCurrentScene();
  disconnect(sceneHandle, SIGNAL(sceneSwitched()), this,
             SLOT(onSceneSwitched()));
  disconnect(sceneHandle, SIGNAL(nameSceneChanged()), this,
             SLOT(changeWindowTitle()));

  TXsheetHandle *xsheetHandle = app->getCurrentXsheet();
  disconnect(xsheetHandle, SIGNAL(xsheetSwitched()), this,
             SLOT(updateAllAree()));
  disconnect(xsheetHandle, SIGNAL(xsheetChanged()), this,
             SLOT(onXsheetChanged()));
  disconnect(xsheetHandle, SIGNAL(xsheetChanged()), this,
             SLOT(changeWindowTitle()));

  disconnect(app->getCurrentSelection(),
             SIGNAL(selectionSwitched(TSelection *, TSelection *)), this,
             SLOT(onSelectionSwitched(TSelection *, TSelection *)));

  disconnect(app->getCurrentSelection(), SIGNAL(selectionChanged(TSelection *)),
             this, SLOT(onSelectionChanged(TSelection *)));

  disconnect(app->getCurrentLevel(), SIGNAL(xshLevelSwitched(TXshLevel *)),
             this, SLOT(changeWindowTitle()));

  disconnect(IconGenerator::instance(), SIGNAL(iconGenerated()), this,
             SLOT(updateColumnArea()));
}

//-----------------------------------------------------------------------------

/*
void XsheetViewer::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  //p.fillRect(visibleRegion().boundingRect().adjusted(1,1,-1,-1),QBrush(grey150));
}*/

//-----------------------------------------------------------------------------

void XsheetViewer::resizeEvent(QResizeEvent *event) {
  positionSections ();

  //(New Layout Manager) introduced automatic refresh
  refreshContentSize(
      0,
      0);  // Don't updateAreeSize because you have to account scrollbars
  updateAllAree();
}

//-----------------------------------------------------------------------------

void XsheetViewer::wheelEvent(QWheelEvent *event) {
  switch (event->source()) {
  case Qt::MouseEventNotSynthesized: {
    int markerDistance = 6, markerOffset = 0;
    TApp::instance()
        ->getCurrentScene()
        ->getScene()
        ->getProperties()
        ->getMarkers(markerDistance, markerOffset);
    if (event->angleDelta().x() == 0) {  // vertical scroll
      int scrollPixels = (event->angleDelta().y() > 0 ? 1 : -1) *
                         markerDistance * XsheetGUI::RowHeight;
      scroll(QPoint(0, -scrollPixels));
    } else {  // horizontal scroll
      int scrollPixels =
          (event->angleDelta().x() > 0 ? 1 : -1) * XsheetGUI::ColumnWidth;
      scroll(QPoint(-scrollPixels, 0));
    }
    break;
  }

  case Qt::MouseEventSynthesizedBySystem:  // macbook touch-pad
  {
    QPoint numPixels  = event->pixelDelta();
    QPoint numDegrees = event->angleDelta() / 8;
    if (!numPixels.isNull()) {
      scroll(-numPixels);
    } else if (!numDegrees.isNull()) {
      QPoint numSteps = numDegrees / 15;
      scroll(-numSteps);
    }
    break;
  }

  default:  // Qt::MouseEventSynthesizedByQt,
            // Qt::MouseEventSynthesizedByApplication
    {
      std::cout << "not supported event: Qt::MouseEventSynthesizedByQt, "
                   "Qt::MouseEventSynthesizedByApplication"
                << std::endl;
      break;
    }

  }  // end switch
}

//-----------------------------------------------------------------------------

void XsheetViewer::keyPressEvent(QKeyEvent *event) {
  struct Locals {
    XsheetViewer *m_this;

    void scrollTo(double y, const QRect &visibleRect) {
      int deltaY = (y < visibleRect.top()) ? y - visibleRect.top()
                                           : y - visibleRect.bottom();

      m_this->scroll(QPoint(0, deltaY));
    }

  } locals = {this};

  if (changeFrameSkippingHolds(event)) return;

  int frameCount = getXsheet()->getFrameCount();
  int row = getCurrentRow(), col = getCurrentColumn();

  switch (int key = event->key()) {
  case Qt::Key_Up:
    setCurrentRow(std::max(row - 1, 0));
    break;
  case Qt::Key_Down:
    setCurrentRow(row + 1);
    break;
  case Qt::Key_Left:
    setCurrentColumn(std::max(col - 1, 0));
    break;
  case Qt::Key_Right:
    setCurrentColumn(col + 1);
    break;
  case Qt::Key_Control:
    // display the upper-directional smart tab only when the ctrl key is pressed
    m_cellArea->onControlPressed(true);
    break;

  default: {
    QRect visibleRect   = m_cellArea->visibleRegion().boundingRect();
    int visibleRowCount = visibleRect.height() / XsheetGUI::RowHeight;

    switch (key) {
    case Qt::Key_PageUp:
      locals.scrollTo(
          visibleRect.top() - visibleRowCount * XsheetGUI::RowHeight,
          visibleRect);
      break;
    case Qt::Key_PageDown:
      locals.scrollTo(
          visibleRect.bottom() + visibleRowCount * XsheetGUI::RowHeight,
          visibleRect);
      break;
    case Qt::Key_Home:
      locals.scrollTo(0, visibleRect);
      break;
    case Qt::Key_End:
      locals.scrollTo((frameCount + 1) * XsheetGUI::RowHeight, visibleRect);
      break;
    }
    break;
  }
  }
}

//-----------------------------------------------------------------------------
// display the upper-directional smart tab only when the ctrl key is pressed
void XsheetViewer::keyReleaseEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Control) m_cellArea->onControlPressed(false);
}

void XsheetViewer::enterEvent(QEvent *) { m_cellArea->onControlPressed(false); }

//-----------------------------------------------------------------------------
/*! scroll the cell area to make a cell at (row,col) visible
*/
void XsheetViewer::scrollTo(int row, int col) {
  QRect visibleRect = m_cellArea->visibleRegion().boundingRect();
  QPoint topLeft = positionToXY (CellPosition (row, col));
  QRect cellRect(topLeft, QSize (XsheetGUI::ColumnWidth, XsheetGUI::RowHeight));

  int deltaX = 0;
  int deltaY = 0;

  if (cellRect.left() < visibleRect.left())
    deltaX = cellRect.left() - visibleRect.left();
  else if (cellRect.right() > visibleRect.right())
    deltaX = cellRect.left() - visibleRect.left();

  if (cellRect.top() < visibleRect.top())
    deltaY = cellRect.top() - visibleRect.top();
  else if (cellRect.bottom() > visibleRect.bottom())
    deltaY = cellRect.bottom() - visibleRect.bottom();

  if (deltaX != 0 || deltaY != 0) {
    scroll(QPoint(deltaX, deltaY));
  }
}

//-----------------------------------------------------------------------------

void XsheetViewer::onSceneSwitched() {
  refreshContentSize(0, 0);
  updateAreeSize();
  updateAllAree();
  clearNoteWidgets();
  m_noteArea->updateButtons();
}

//-----------------------------------------------------------------------------

void XsheetViewer::onXsheetChanged() {
  refreshContentSize(0, 0);
  updateAllAree();
}

//-----------------------------------------------------------------------------

void XsheetViewer::onCurrentFrameSwitched() {
  int row           = TApp::instance()->getCurrentFrame()->getFrame();
  QRect visibleRect = m_cellArea->visibleRegion().boundingRect();
  if (visibleRect.isEmpty()) {
    m_isCurrentFrameSwitched = true;
    return;
  }
  m_isCurrentFrameSwitched = false;
  scrollToRow(row);
}

//-----------------------------------------------------------------------------

void XsheetViewer::onPlayingStatusChanged() {
  if (!Preferences::instance()->isXsheetAutopanEnabled())
    onCurrentFrameSwitched();
}

//-----------------------------------------------------------------------------

void XsheetViewer::onCurrentColumnSwitched() {
  int col           = TApp::instance()->getCurrentColumn()->getColumnIndex();
  QRect visibleRect = m_columnArea->visibleRegion().boundingRect();
  if (visibleRect.isEmpty()) {
    m_isCurrentColumnSwitched = true;
    return;
  }
  m_isCurrentColumnSwitched = false;
  scrollToColumn(col);
}

//-----------------------------------------------------------------------------

void XsheetViewer::scrollToColumn(int col) {
	int x0 = columnToLayerAxis (col);
	int x1 = columnToLayerAxis (col + 1);

	if (orientation ()->isVerticalTimeline ())
		scrollToHorizontalRange (x0, x1);
	else
		scrollToVerticalRange (x0, x1);
}

//-----------------------------------------------------------------------------

void XsheetViewer::scrollToHorizontalRange(int x0, int x1) {
  QRect visibleRect = m_cellArea->visibleRegion().boundingRect();
  if (visibleRect.isEmpty ())
    return;
  int visibleLeft   = visibleRect.left();
  int visibleRight  = visibleRect.right();

  if (visibleLeft > x0) {  // If they are out of left visible region
    int deltaX = x0 - visibleLeft;
    scroll(QPoint(deltaX, 0));
    return;
  }
  if (visibleRight < x1) {  // If they are out of right visible region
    int deltaX = x1 + 2 - visibleRight;
    scroll(QPoint(deltaX, 0));
    return;
  }
  updateCellColumnAree();
}

//-----------------------------------------------------------------------------

void XsheetViewer::scrollToRow(int row) {
	int y0 = rowToFrameAxis (row);
	int y1 = rowToFrameAxis (row + 1);

	if (orientation ()->isVerticalTimeline ())
		scrollToVerticalRange (y0, y1);
	else
		scrollToHorizontalRange (y0, y1);
}

//-----------------------------------------------------------------------------

void XsheetViewer::scrollToVerticalRange(int y0, int y1) {
	int yMin = min (y0, y1);
	int yMax = max (y0, y1);
	QRect visibleRect = m_cellArea->visibleRegion().boundingRect();
  if (visibleRect.isEmpty ())
    return;
  int visibleTop    = visibleRect.top();
  int visibleBottom = visibleRect.bottom();

  if (visibleTop > yMin) {  // If they are out of top visible region
    int deltaY = yMin - visibleTop;
    if (!TApp::instance()->getCurrentFrame()->isPlaying() ||
        Preferences::instance()->isXsheetAutopanEnabled()) {
      scroll(QPoint(0, deltaY));
      return;
    }
  }

  if (visibleBottom < yMax) {  // If they are out of bottom visible region
    int deltaY = yMax + 2 - visibleBottom;
    if (!TApp::instance()->getCurrentFrame()->isPlaying() ||
        Preferences::instance()->isXsheetAutopanEnabled()) {
      scroll(QPoint(0, deltaY));
      return;
    }
  }
  m_rowArea->update(m_rowArea->visibleRegion());
  m_cellArea->update(m_cellArea->visibleRegion());
}

//-----------------------------------------------------------------------------

void XsheetViewer::onSelectionSwitched(TSelection *oldSelection,
                                       TSelection *newSelection) {
  if ((TSelection *)getCellSelection() == oldSelection) {
    m_cellArea->update(m_cellArea->visibleRegion());
    m_rowArea->update(m_rowArea->visibleRegion());
    changeWindowTitle();
  } else if ((TSelection *)m_columnSelection == oldSelection)
    m_columnArea->update(m_columnArea->visibleRegion());
}

//-----------------------------------------------------------------------------
/*! update display of the cell selection range in title bar
*/
void XsheetViewer::onSelectionChanged(TSelection *selection) {
  if ((TSelection *)getCellSelection() == selection) changeWindowTitle();
}

//-----------------------------------------------------------------------------

void XsheetViewer::updateAllAree(bool isDragging) {
  m_cellArea->update(m_cellArea->visibleRegion());
  if (!isDragging) {
    m_rowArea->update(m_rowArea->visibleRegion());
    m_columnArea->update(m_columnArea->visibleRegion());
  }
}

//-----------------------------------------------------------------------------

void XsheetViewer::updateColumnArea() {
  m_columnArea->update(m_columnArea->visibleRegion());
}

//-----------------------------------------------------------------------------

void XsheetViewer::updateCellColumnAree() {
  m_columnArea->update(m_columnArea->visibleRegion());
  m_cellArea->update(m_cellArea->visibleRegion());
}

//-----------------------------------------------------------------------------

void XsheetViewer::updateCellRowAree() {
  m_rowArea->update(m_rowArea->visibleRegion());
  m_cellArea->update(m_cellArea->visibleRegion());
}

//-----------------------------------------------------------------------------

void XsheetViewer::onScrubStopped() {
  resetScrubHighlight();
  updateCells();
}

//-----------------------------------------------------------------------------

void XsheetViewer::discardNoteWidget() {
  if (m_currentNoteIndex == -1) return;
  TXshNoteSet *notes = getXsheet()->getNotes();
  int i;
  for (i = m_currentNoteIndex + 1; i < m_noteWidgets.size(); i++) {
    XsheetGUI::NoteWidget *w = m_noteWidgets.at(i);
    int index                = w->getNoteIndex();
    w->setNoteIndex(index - 1);
    w->update();
  }
  delete m_noteWidgets.at(m_currentNoteIndex);
  m_noteWidgets.removeAt(m_currentNoteIndex);
  m_noteArea->updateButtons();
  updateCells();
}

//-----------------------------------------------------------------------------

QList<XsheetGUI::NoteWidget *> XsheetViewer::getNotesWidget() const {
  return m_noteWidgets;
}

//-----------------------------------------------------------------------------

void XsheetViewer::addNoteWidget(XsheetGUI::NoteWidget *w) {
  m_noteWidgets.push_back(w);
  m_noteArea->updateButtons();
}

//-----------------------------------------------------------------------------

int XsheetViewer::getCurrentNoteIndex() const { return m_currentNoteIndex; }

//-----------------------------------------------------------------------------

void XsheetViewer::setCurrentNoteIndex(int currentNoteIndex) {
  m_currentNoteIndex = currentNoteIndex;
  m_noteArea->updateButtons();

  if (currentNoteIndex < 0) return;

  TXshNoteSet *notes = getXsheet()->getNotes();
  int row            = notes->getNoteRow(currentNoteIndex);
  int col            = notes->getNoteCol(currentNoteIndex);
  TPointD pos        = notes->getNotePos(currentNoteIndex);

  QPoint topLeft = positionToXY (CellPosition (row, col)) + QPoint (pos.x, pos.y); // actually xy
  QSize size (XsheetGUI::NoteWidth, XsheetGUI::NoteHeight);
  QRect noteRect (topLeft, size);
  scrollToHorizontalRange(noteRect.left (), noteRect.right ());
  scrollToVerticalRange(noteRect.top (), noteRect.bottom ());
}

//-----------------------------------------------------------------------------

void XsheetViewer::resetXsheetNotes() { m_noteArea->updateButtons(); }

//-----------------------------------------------------------------------------

void XsheetViewer::updateNoteWidgets() {
  int i;
  for (i = 0; i < m_noteWidgets.size(); i++) m_noteWidgets.at(i)->update();
  m_noteArea->updatePopup();
  updateCells();
}

//-----------------------------------------------------------------------------

void XsheetViewer::clearNoteWidgets() {
  int i;
  for (i = 0; i < m_noteWidgets.size(); i++) delete m_noteWidgets.at(i);
  m_noteWidgets.clear();
}

//-----------------------------------------------------------------------------

void XsheetViewer::changeWindowTitle() {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();
  if (!scene || !app->getCurrentFrame()->isEditingScene()) return;
  QString sceneName = QString::fromStdWString(scene->getSceneName());
  if (sceneName.isEmpty()) sceneName = tr("Untitled");
  if (app->getCurrentScene()->getDirtyFlag()) sceneName += QString("*");
  QString name   = tr("Scene: ") + sceneName;
  int frameCount = scene->getFrameCount();
  name           = name + "   ::   " + tr(std::to_string(frameCount).c_str()) +
         tr(" Frames");

  // subXsheet or not
  ChildStack *childStack = scene->getChildStack();
  if (childStack && childStack->getAncestorCount() > 0) {
    name += tr("  (Sub)");
  }
  // current level name
  TXshLevel *level = app->getCurrentLevel()->getLevel();
  if (level) {
    QString levelName = QString::fromStdWString(level->getName());
    name += tr("  Level: ") + levelName;
  }
  // cell selection range
  if ((TSelection *)getCellSelection() ==
          app->getCurrentSelection()->getSelection() &&
      !getCellSelection()->isEmpty()) {
    int r0, r1, c0, c1;
    getCellSelection()->getSelectedCells(r0, c0, r1, c1);
    name += tr("   Selected: ") + QString::number(r1 - r0 + 1) +
            ((r1 - r0 + 1 == 1) ? tr(" frame : ") : tr(" frames * ")) +
            QString::number(c1 - c0 + 1) +
            ((c1 - c0 + 1 == 1) ? tr(" column") : tr(" columns"));
  }

  parentWidget()->setWindowTitle(name);
}

//-----------------------------------------------------------------------------
/*! convert the last one digit of the frame number to alphabet
        Ex.  12 -> 1B    21 -> 2A   30 -> 3
 */
QString XsheetViewer::getFrameNumberWithLetters(int frame) {
  int letterNum = frame % 10;
  QChar letter;

  switch (letterNum) {
  case 0:
    letter = QChar();
    break;
  case 1:
    letter = 'A';
    break;
  case 2:
    letter = 'B';
    break;
  case 3:
    letter = 'C';
    break;
  case 4:
    letter = 'D';
    break;
  case 5:
    letter = 'E';
    break;
  case 6:
    letter = 'F';
    break;
  case 7:
    letter = 'G';
    break;
  case 8:
    letter = 'H';
    break;
  case 9:
    letter = 'I';
    break;
  default:
    letter = QChar();
    break;
  }

  QString number;
  if (frame >= 10) {
    number = QString::number(frame);
    number.chop(1);
  } else
    number = "0";

  return (QChar(letter).isNull()) ? number : number.append(letter);
}
//-----------------------------------------------------------------------------

void XsheetViewer::setFrameDisplayStyle(FrameDisplayStyle style) {
  m_frameDisplayStyle              = style;
  FrameDisplayStyleInXsheetRowArea = (int)style;
}

//-----------------------------------------------------------------------------
/*
TPanel *createXsheetViewer(QWidget *parent)
{
  TPanel *panel = new TPanel(parent);
  panel->setPanelType("Xsheet");
  panel->setWidget(new XsheetViewer(panel));
  return panel;
}
*/

//=============================================================================
// XSheetViewerCommand
//-----------------------------------------------------------------------------

OpenFloatingPanel openXsheetViewerCommand(MI_OpenXshView, "Xsheet",
                                          QObject::tr("Xsheet"));
