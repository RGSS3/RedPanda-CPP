/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "SynEdit.h"
#include "highlighter/cpp.h"
#include <QApplication>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <QScrollBar>
#include <QPaintEvent>
#include <QPainter>
#include <QTimerEvent>
#include "highlighter/base.h"
#include "Constants.h"
#include "TextPainter.h"
#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QStyleHints>
#include <QMessageBox>
#include <QDrag>
#include <QMimeData>
#include <QDesktopWidget>
#include <QTextEdit>
#include <QMimeData>

SynEdit::SynEdit(QWidget *parent) : QAbstractScrollArea(parent),
    mDropped(false)
{
    mCharWidth=1;
    mTextHeight = 1;
    mLastKey = 0;
    mLastKeyModifiers = Qt::NoModifier;
    mModified = false;
    mPaintLock = 0;
    mPainterLock = 0;
    mPainting = false;
#ifdef Q_OS_WIN
    mFontDummy = QFont("Consolas",12);
#elif defined(Q_OS_LINUX)
    mFontDummy = QFont("terminal",14);
#elif defined(Q_OS_MACOS)
    mFontDummy = QFont("Menlo", 14);
#else
#error "Not supported!"
#endif
    mFontDummy.setStyleStrategy(QFont::PreferAntialias);
    mDocument = std::make_shared<SynDocument>(mFontDummy, this);
    //fPlugins := TList.Create;
    mMouseMoved = false;
    mUndoing = false;
    mDocument->connect(mDocument.get(), &SynDocument::changed, this, &SynEdit::onLinesChanged);
    mDocument->connect(mDocument.get(), &SynDocument::changing, this, &SynEdit::onLinesChanging);
    mDocument->connect(mDocument.get(), &SynDocument::cleared, this, &SynEdit::onLinesCleared);
    mDocument->connect(mDocument.get(), &SynDocument::deleted, this, &SynEdit::onLinesDeleted);
    mDocument->connect(mDocument.get(), &SynDocument::inserted, this, &SynEdit::onLinesInserted);
    mDocument->connect(mDocument.get(), &SynDocument::putted, this, &SynEdit::onLinesPutted);

    mGutterWidth = 0;
    mScrollBars = SynScrollStyle::ssBoth;



    mUndoList = std::make_shared<SynEditUndoList>();
    mUndoList->connect(mUndoList.get(), &SynEditUndoList::addedUndo, this, &SynEdit::onUndoAdded);
    mRedoList = std::make_shared<SynEditUndoList>();
    mRedoList->connect(mRedoList.get(), &SynEditUndoList::addedUndo, this, &SynEdit::onRedoAdded);

    mForegroundColor=palette().color(QPalette::Text);
    mBackgroundColor=palette().color(QPalette::Base);
    mCaretColor = Qt::red;
    mCaretUseTextColor = false;
    mActiveLineColor = Qt::blue;
    mSelectedBackground = palette().color(QPalette::Highlight);
    mSelectedForeground = palette().color(QPalette::HighlightedText);

    mBookMarkOpt.connect(&mBookMarkOpt, &SynBookMarkOpt::changed, this, &SynEdit::onBookMarkOptionsChanged);
    //  fRightEdge has to be set before FontChanged is called for the first time
    mRightEdge = 80;

    mMouseWheelScrollSpeed = 3;
    mMouseSelectionScrollSpeed = 1;

    mGutter.setRightOffset(21);
    mGutter.connect(&mGutter, &SynGutter::changed, this, &SynEdit::onGutterChanged);
    mGutterWidth = mGutter.realGutterWidth(charWidth());
    //ControlStyle := ControlStyle + [csOpaque, csSetCaption, csNeedsBorderPaint];
    //Height := 150;
    //Width := 200;
    this->setCursor(Qt::CursorShape::IBeamCursor);
    //TabStop := True;
    mInserting = true;
    mExtraLineSpacing = 0;

    this->setFrameShape(QFrame::Panel);
    this->setFrameShadow(QFrame::Sunken);
    this->setLineWidth(1);
    mInsertCaret = SynEditCaretType::ctVerticalLine;
    mOverwriteCaret = SynEditCaretType::ctBlock;
    mSelectionMode = SynSelectionMode::smNormal;
    mActiveSelectionMode = SynSelectionMode::smNormal;
    mReadOnly = false;

    //stop qt to auto fill background
    setAutoFillBackground(false);
    //fFocusList := TList.Create;
    //fKbdHandler := TSynEditKbdHandler.Create;
    //fMarkList.OnChange := MarkListChange;
    setDefaultKeystrokes();
    mRightEdgeColor = Qt::lightGray;

    mWantReturns = true;
    mWantTabs = false;
    mLeftChar = 1;
    mTopLine = 1;
    mCaretX = 1;
    mLastCaretColumn = 1;
    mCaretY = 1;
    mBlockBegin.Char = 1;
    mBlockBegin.Line = 1;
    mBlockEnd = mBlockBegin;
    mOptions = eoAutoIndent
            | eoDragDropEditing | eoEnhanceEndKey | eoTabIndent |
             eoGroupUndo | eoKeepCaretX | eoSelectWordByDblClick
            | eoHideShowScrollbars ;

    mScrollTimer = new QTimer(this);
    //mScrollTimer->setInterval(100);
    connect(mScrollTimer, &QTimer::timeout,this, &SynEdit::onScrollTimeout);

    mScrollHintColor = Qt::yellow;
    mScrollHintFormat = SynScrollHintFormat::shfTopLineOnly;

    qreal dpr=devicePixelRatioF();
    mContentImage = std::make_shared<QImage>(clientWidth()*dpr,clientHeight()*dpr,QImage::Format_ARGB32);
    mContentImage->setDevicePixelRatio(dpr);

    mUseCodeFolding = true;
    m_blinkTimerId = 0;
    m_blinkStatus = 0;

    hideCaret();

    connect(horizontalScrollBar(),&QScrollBar::valueChanged,
            this, &SynEdit::onScrolled);
    connect(verticalScrollBar(),&QScrollBar::valueChanged,
            this, &SynEdit::onScrolled);
    //enable input method
    setAttribute(Qt::WA_InputMethodEnabled);

    //setMouseTracking(true);
    setAcceptDrops(true);

    setFont(mFontDummy);
    setFontForNonAscii(mFontDummy);
}

int SynEdit::displayLineCount() const
{
    if (mDocument->empty()) {
        return 0;
    }
    return lineToRow(mDocument->count());
}

DisplayCoord SynEdit::displayXY() const
{
    return bufferToDisplayPos(caretXY());
}

int SynEdit::displayX() const
{
    return displayXY().Column;
}

int SynEdit::displayY() const
{
    return displayXY().Row;
}

BufferCoord SynEdit::caretXY() const
{
    BufferCoord result;
    result.Char = caretX();
    result.Line = caretY();
    return result;
}

int SynEdit::caretX() const
{
    return mCaretX;
}

int SynEdit::caretY() const
{
    return mCaretY;
}

void SynEdit::setCaretX(int value)
{
    setCaretXY({value,mCaretY});
}

void SynEdit::setCaretY(int value)
{
    setCaretXY({mCaretX,value});
}

void SynEdit::setCaretXY(const BufferCoord &value)
{
    setBlockBegin(value);
    setBlockEnd(value);
    setCaretXYEx(true,value);
}

void SynEdit::setCaretXYEx(bool CallEnsureCursorPosVisible, BufferCoord value)
{
    bool vTriggerPaint=true; //how to test it?

    if (vTriggerPaint)
        doOnPaintTransient(SynTransientType::ttBefore);
    int nMaxX;
    if (value.Line > mDocument->count())
        value.Line = mDocument->count();
    if (mActiveSelectionMode!=SynSelectionMode::smColumn) {
        if (value.Line < 1) {
            // this is just to make sure if Lines stringlist should be empty
            value.Line = 1;
            if (!mOptions.testFlag(SynEditorOption::eoScrollPastEol)) {
                nMaxX = 1;
            } else {
                nMaxX = getDisplayStringAtLine(value.Line).length()+1;
            }
        } else {
            nMaxX = getDisplayStringAtLine(value.Line).length()+1;
        }
        value.Char = std::min(value.Char,nMaxX);
    }
    value.Char = std::max(value.Char,1);
//    if ((value.Char > nMaxX) && (! (mOptions.testFlag(SynEditorOption::eoScrollPastEol)) ) )
//        value.Char = nMaxX;
//    if (value.Char < 1)
//        value.Char = 1;
    if ((value.Char != mCaretX) || (value.Line != mCaretY)) {
        incPaintLock();
        auto action = finally([this]{
            decPaintLock();
        });
        // simply include the flags, fPaintLock is > 0
        if (mCaretX != value.Char) {
            mCaretX = value.Char;
            mStatusChanges.setFlag(SynStatusChange::scCaretX);
            invalidateLine(mCaretY);
        }
        if (mCaretY != value.Line) {
            int oldCaretY = mCaretY;
            mCaretY = value.Line;
            invalidateLine(mCaretY);
            invalidateGutterLine(mCaretY);
            invalidateLine(oldCaretY);
            invalidateGutterLine(oldCaretY);
            mStatusChanges.setFlag(SynStatusChange::scCaretY);
        }
        // Call UpdateLastCaretX before DecPaintLock because the event handler it
        // calls could raise an exception, and we don't want fLastCaretX to be
        // left in an undefined state if that happens.
        updateLastCaretX();
        if (CallEnsureCursorPosVisible)
            ensureCursorPosVisible();
        mStateFlags.setFlag(SynStateFlag::sfCaretChanged);
        mStateFlags.setFlag(SynStateFlag::sfScrollbarChanged);
    } else {
      // Also call UpdateLastCaretX if the caret didn't move. Apps don't know
      // anything about fLastCaretX and they shouldn't need to. So, to avoid any
      // unwanted surprises, always update fLastCaretX whenever CaretXY is
      // assigned to.
      // Note to SynEdit developers: If this is undesirable in some obscure
      // case, just save the value of fLastCaretX before assigning to CaretXY and
      // restore it afterward as appropriate.
      updateLastCaretX();
    }
    if (vTriggerPaint)
      doOnPaintTransient(SynTransientType::ttAfter);

}

void SynEdit::setCaretXYCentered(const BufferCoord &value)
{
    incPaintLock();
    auto action = finally([this] {
        decPaintLock();
    });
    mStatusChanges.setFlag(SynStatusChange::scSelection);
    setCaretXYEx(false,value);
    if (selAvail())
        invalidateSelection();
    mBlockBegin.Char = mCaretX;
    mBlockBegin.Line = mCaretY;
    mBlockEnd = mBlockBegin;
    ensureCursorPosVisibleEx(true); // but here after block has been set
}

void SynEdit::uncollapseAroundLine(int line)
{
    while (true) { // Open up the closed folds around the focused line until we can see the line we're looking for
      PSynEditFoldRange fold = foldHidesLine(line);
      if (fold)
          uncollapse(fold);
      else
          break;
    }
}

PSynEditFoldRange SynEdit::foldHidesLine(int line)
{
    return foldAroundLineEx(line, true, false, true);
}

void SynEdit::setInsertMode(bool value)
{
    if (mInserting != value) {
        mInserting = value;
        updateCaret();
        emit statusChanged(scInsertMode);
    }
}

bool SynEdit::insertMode() const
{
    return mInserting;
}

bool SynEdit::canUndo() const
{
    return !mReadOnly && mUndoList->CanUndo();
}

bool SynEdit::canRedo() const
{
    return !mReadOnly && mRedoList->CanUndo();
}

int SynEdit::maxScrollWidth() const
{
    int maxLen = mDocument->lengthOfLongestLine();
    if (highlighter())
        maxLen = maxLen+stringColumns(highlighter()->foldString(),maxLen);
    if (mOptions.testFlag(eoScrollPastEol))
        return std::max(maxLen ,1);
    else
        return std::max(maxLen-mCharsInWindow+1, 1);
}

bool SynEdit::getHighlighterAttriAtRowCol(const BufferCoord &XY, QString &Token, PSynHighlighterAttribute &Attri)
{
    SynHighlighterTokenType TmpType;
    int TmpKind, TmpStart;
    return getHighlighterAttriAtRowColEx(XY, Token, TmpType, TmpKind,TmpStart, Attri);
}

bool SynEdit::getHighlighterAttriAtRowCol(const BufferCoord &XY, QString &Token, bool &tokenFinished, SynHighlighterTokenType &TokenType, PSynHighlighterAttribute &Attri)
{
    int PosX, PosY, endPos, Start;
    QString Line;
    PosY = XY.Line - 1;
    if (mHighlighter && (PosY >= 0) && (PosY < mDocument->count())) {
        Line = mDocument->getString(PosY);
        if (PosY == 0) {
            mHighlighter->resetState();
        } else {
            mHighlighter->setState(mDocument->ranges(PosY-1));
        }
        mHighlighter->setLine(Line, PosY);
        PosX = XY.Char;
        if ((PosX > 0) && (PosX <= Line.length())) {
            while (!mHighlighter->eol()) {
                Start = mHighlighter->getTokenPos() + 1;
                Token = mHighlighter->getToken();
                endPos = Start + Token.length()-1;
                if ((PosX >= Start) && (PosX <= endPos)) {
                    Attri = mHighlighter->getTokenAttribute();
                    if (PosX == endPos)
                        tokenFinished = mHighlighter->getTokenFinished();
                    else
                        tokenFinished = false;
                    TokenType = mHighlighter->getTokenType();
                    return true;
                }
                mHighlighter->next();
            }
        }
    }
    Token = "";
    Attri = PSynHighlighterAttribute();
    tokenFinished = false;
    return false;
}

bool SynEdit::getHighlighterAttriAtRowColEx(const BufferCoord &XY, QString &Token, SynHighlighterTokenType &TokenType, SynTokenKind &TokenKind, int &Start, PSynHighlighterAttribute &Attri)
{
    int PosX, PosY, endPos;
    QString Line;
    PosY = XY.Line - 1;
    if (mHighlighter && (PosY >= 0) && (PosY < mDocument->count())) {
        Line = mDocument->getString(PosY);
        if (PosY == 0) {
            mHighlighter->resetState();
        } else {
            mHighlighter->setState(mDocument->ranges(PosY-1));
        }
        mHighlighter->setLine(Line, PosY);
        PosX = XY.Char;
        if ((PosX > 0) && (PosX <= Line.length())) {
            while (!mHighlighter->eol()) {
                Start = mHighlighter->getTokenPos() + 1;
                Token = mHighlighter->getToken();
                endPos = Start + Token.length()-1;
                if ((PosX >= Start) && (PosX <= endPos)) {
                    Attri = mHighlighter->getTokenAttribute();
                    TokenKind = mHighlighter->getTokenKind();
                    TokenType = mHighlighter->getTokenType();
                    return true;
                }
                mHighlighter->next();
            }
        }
    }
    Token = "";
    Attri = PSynHighlighterAttribute();
    TokenKind = 0;
    TokenType = SynHighlighterTokenType::Default;
    return false;
}

void SynEdit::beginUndoBlock()
{
    mUndoList->BeginBlock();
}

void SynEdit::endUndoBlock()
{
    mUndoList->EndBlock();
}

void SynEdit::addCaretToUndo()
{
    BufferCoord p=caretXY();
    mUndoList->AddChange(SynChangeReason::crCaret,p,p,QStringList(), mActiveSelectionMode);
}

void SynEdit::addLeftTopToUndo()
{
    BufferCoord p;
    p.Char = leftChar();
    p.Line = topLine();
    mUndoList->AddChange(SynChangeReason::crLeftTop,p,p,QStringList(), mActiveSelectionMode);
}

void SynEdit::addSelectionToUndo()
{
    mUndoList->AddChange(SynChangeReason::crSelection,mBlockBegin,
                         mBlockEnd,QStringList(),mActiveSelectionMode);
}

void SynEdit::beginUpdate()
{
    incPaintLock();
}

void SynEdit::endUpdate()
{
    decPaintLock();
}

BufferCoord SynEdit::getMatchingBracket()
{
    return getMatchingBracketEx(caretXY());
}

BufferCoord SynEdit::getMatchingBracketEx(BufferCoord APoint)
{
    QChar Brackets[] = {'(', ')', '[', ']', '{', '}', '<', '>'};
    QString Line;
    int i, PosX, PosY, Len;
    QChar Test, BracketInc, BracketDec;
    int NumBrackets;
    QString vDummy;
    PSynHighlighterAttribute attr;
    BufferCoord p;
    bool isCommentOrStringOrChar;
    int nBrackets = sizeof(Brackets) / sizeof(QChar);

    if (mDocument->count()<1)
        return BufferCoord{0,0};
    // get char at caret
    PosX = std::max(APoint.Char,1);
    PosY = std::max(APoint.Line,1);
    Line = mDocument->getString(APoint.Line - 1);
    if (Line.length() >= PosX ) {
        Test = Line[PosX-1];
        // is it one of the recognized brackets?
        for (i = 0; i<nBrackets; i++) {
            if (Test == Brackets[i]) {
                // this is the bracket, get the matching one and the direction
                BracketInc = Brackets[i];
                BracketDec = Brackets[i ^ 1]; // 0 -> 1, 1 -> 0, ...
                // search for the matching bracket (that is until NumBrackets = 0)
                NumBrackets = 1;
                if (i%2==1) {
                    while (true) {
                        // search until start of line
                        while (PosX > 1) {
                            PosX--;
                            Test = Line[PosX-1];
                            p.Char = PosX;
                            p.Line = PosY;
                            if ((Test == BracketInc) || (Test == BracketDec)) {
                                isCommentOrStringOrChar = false;
                                if (getHighlighterAttriAtRowCol(p, vDummy, attr))
                                    isCommentOrStringOrChar =
                                        (attr == mHighlighter->stringAttribute()) ||
                                            (attr == mHighlighter->commentAttribute()) ||
                                            (attr->name() == SYNS_AttrCharacter);
                                if ((Test == BracketInc) && (!isCommentOrStringOrChar))
                                    NumBrackets++;
                                else if ((Test == BracketDec) && (!isCommentOrStringOrChar)) {
                                    NumBrackets--;
                                    if (NumBrackets == 0) {
                                        // matching bracket found, set caret and bail out
                                        return p;
                                    }
                                }
                            }
                        }
                        // get previous line if possible
                        if (PosY == 1)
                            break;
                        PosY--;
                        Line = mDocument->getString(PosY - 1);
                        PosX = Line.length() + 1;
                    }
                } else {
                    while (true) {
                        // search until end of line
                        Len = Line.length();
                        while (PosX < Len) {
                            PosX++;
                            Test = Line[PosX-1];
                            p.Char = PosX;
                            p.Line = PosY;
                            if ((Test == BracketInc) || (Test == BracketDec)) {
                                isCommentOrStringOrChar = false;
                                if (getHighlighterAttriAtRowCol(p, vDummy, attr))
                                    isCommentOrStringOrChar =
                                        (attr == mHighlighter->stringAttribute()) ||
                                            (attr == mHighlighter->commentAttribute()) ||
                                            (attr->name() == SYNS_AttrCharacter);
                                else
                                    isCommentOrStringOrChar = false;
                                if ((Test == BracketInc) && (!isCommentOrStringOrChar))
                                    NumBrackets++;
                                else if ((Test == BracketDec) && (!isCommentOrStringOrChar)) {
                                    NumBrackets--;
                                    if (NumBrackets == 0) {
                                        // matching bracket found, set caret and bail out
                                        return p;
                                    }
                                }
                            }
                        }
                        // get next line if possible
                        if (PosY == mDocument->count())
                            break;
                        PosY++;
                        Line = mDocument->getString(PosY - 1);
                        PosX = 0;
                    }
                }
                // don't test the other brackets, we're done
                break;
            }
        }
    }
    return BufferCoord{0,0};
}

QStringList SynEdit::contents()
{
    return document()->contents();
}

QString SynEdit::text()
{
    return document()->text();
}

bool SynEdit::getPositionOfMouse(BufferCoord &aPos)
{
    QPoint point = QCursor::pos();
    point = mapFromGlobal(point);
    return pointToCharLine(point,aPos);
}

bool SynEdit::getLineOfMouse(int &line)
{
    QPoint point = QCursor::pos();
    point = mapFromGlobal(point);
    return pointToLine(point,line);
}

bool SynEdit::pointToCharLine(const QPoint &point, BufferCoord &coord)
{
    // Make sure it fits within the SynEdit bounds (and on the gutter)
    if ((point.x() < gutterWidth() + clientLeft())
            || (point.x()>clientWidth()+clientLeft())
            || (point.y() < clientTop())
            || (point.y() > clientTop()+clientHeight())) {
        return false;
    }

    coord = displayToBufferPos(pixelsToNearestRowColumn(point.x(),point.y()));
    return true;
}

bool SynEdit::pointToLine(const QPoint &point, int &line)
{
    // Make sure it fits within the SynEdit bounds
    if ((point.x() < clientLeft())
            || (point.x()>clientWidth()+clientLeft())
            || (point.y() < clientTop())
            || (point.y() > clientTop()+clientHeight())) {
        return false;
    }

    BufferCoord coord = displayToBufferPos(pixelsToNearestRowColumn(point.x(),point.y()));
    line = coord.Line;
    return true;
}

void SynEdit::invalidateGutter()
{
    invalidateGutterLines(-1, -1);
}

void SynEdit::invalidateGutterLine(int aLine)
{
    if ((aLine < 1) || (aLine > mDocument->count()))
        return;

    invalidateGutterLines(aLine, aLine);
}

void SynEdit::invalidateGutterLines(int FirstLine, int LastLine)
{
    QRect rcInval;
    if (!isVisible())
        return;
    if (FirstLine == -1 && LastLine == -1) {
        rcInval = QRect(0, 0, mGutterWidth, clientHeight());
        if (mStateFlags.testFlag(SynStateFlag::sfLinesChanging))
            mInvalidateRect = mInvalidateRect.united(rcInval);
        else
            invalidateRect(rcInval);
    } else {
        // find the visible lines first
        if (LastLine < FirstLine)
            std::swap(LastLine, FirstLine);
        if (mUseCodeFolding) {
            FirstLine = lineToRow(FirstLine);
            if (LastLine <= mDocument->count())
              LastLine = lineToRow(LastLine);
            else
              LastLine = INT_MAX;
        }
        FirstLine = std::max(FirstLine, mTopLine);
        LastLine = std::min(LastLine, mTopLine + mLinesInWindow);
        // any line visible?
        if (LastLine >= FirstLine) {
            rcInval = {0, mTextHeight * (FirstLine - mTopLine),
                       mGutterWidth, mTextHeight * (LastLine - mTopLine + 1)};
            if (mStateFlags.testFlag(SynStateFlag::sfLinesChanging)) {
                mInvalidateRect =  mInvalidateRect.united(rcInval);
            } else {
                invalidateRect(rcInval);
            }
        }
    }
}

/**
 * @brief Convert point on the edit (x,y) to (row,column)
 * @param aX
 * @param aY
 * @return
 */

DisplayCoord SynEdit::pixelsToNearestRowColumn(int aX, int aY) const
{
    return {
        std::max(1, (int)(mLeftChar + round((aX - mGutterWidth - 2.0) / mCharWidth))),
        std::max(1, mTopLine + (aY / mTextHeight))
    };
}

DisplayCoord SynEdit::pixelsToRowColumn(int aX, int aY) const
{
    return {
        std::max(1, (int)(mLeftChar + (aX - mGutterWidth - 2.0) / mCharWidth)),
        std::max(1, mTopLine + (aY / mTextHeight))
    };

}

QPoint SynEdit::rowColumnToPixels(const DisplayCoord &coord) const
{
    QPoint result;
    result.setX((coord.Column - 1) * mCharWidth + textOffset());
    result.setY((coord.Row - mTopLine) * mTextHeight);
    return result;
}

/**
 * @brief takes a position in the text and transforms it into
 *  the row and column it appears to be on the screen
 * @param p
 * @return
 */
DisplayCoord SynEdit::bufferToDisplayPos(const BufferCoord &p) const
{
    DisplayCoord result {p.Char,p.Line};
    // Account for tabs and charColumns
    if (p.Line-1 <mDocument->count())
        result.Column = charToColumn(p.Line,p.Char);
    // Account for code folding
    if (mUseCodeFolding)
        result.Row = foldLineToRow(result.Row);
    return result;
}

/**
 * @brief takes a position on screen and transfrom it into position of text
 * @param p
 * @return
 */
BufferCoord SynEdit::displayToBufferPos(const DisplayCoord &p) const
{
    BufferCoord Result{p.Column,p.Row};
    // Account for code folding
    if (mUseCodeFolding)
        Result.Line = foldRowToLine(p.Row);
    // Account for tabs
    if (Result.Line <= mDocument->count() ) {
        Result.Char = columnToChar(Result.Line,p.Column);
    }
    return Result;
}

ContentsCoord SynEdit::fromBufferCoord(const BufferCoord &p) const
{
    return createNormalizedBufferCoord(p.Char,p.Line);
}

ContentsCoord SynEdit::createNormalizedBufferCoord(int aChar, int aLine) const
{
    return ContentsCoord(this,aChar,aLine);
}

//QStringList SynEdit::getContents(const ContentsCoord &pStart, const ContentsCoord &pEnd)
//{
//    QStringList result;
//    if (mDocument->count()==0)
//        return result;
//    if (pStart.line()>0) {
//        QString s = mDocument->getString(pStart.line()-1);
//        result += s.mid(pStart.ch()-1);
//    }
//    int endLine = std::min(pEnd.line(),mDocument->count());
//    for (int i=pStart.line();i<endLine-1;i++) {
//        result += mDocument->getString(i);
//    }
//    if (pEnd.line()<=mDocument->count()) {
//        result += mDocument->getString(pEnd.line()-1).mid(0,pEnd.ch()-1);
//    }
//    return result;
//}

//QString SynEdit::getJoinedContents(const ContentsCoord &pStart, const ContentsCoord &pEnd, const QString &joinStr)
//{
//    return getContents(pStart,pEnd).join(joinStr);
//}

int SynEdit::leftSpaces(const QString &line) const
{
    int result = 0;
    if (mOptions.testFlag(eoAutoIndent)) {
        for (QChar ch:line) {
            if (ch == '\t') {
                result += tabWidth() - (result % tabWidth());
            } else if (ch == ' ') {
                result ++;
            } else {
                break;
            }
        }
    }
    return result;
}

QString SynEdit::GetLeftSpacing(int charCount, bool wantTabs) const
{
    if (wantTabs && !mOptions.testFlag(eoTabsToSpaces) && tabWidth()>0) {
        return QString(charCount / tabWidth(),'\t') + QString(charCount % tabWidth(),' ');
    } else {
        return QString(charCount,' ');
    }
}

int SynEdit::charToColumn(int aLine, int aChar) const
{
    if (aLine>=1 && aLine <= mDocument->count()) {
        QString s = getDisplayStringAtLine(aLine);
        return charToColumn(s,aChar);
    }
    return aChar;
}

int SynEdit::charToColumn(const QString &s, int aChar) const
{
    int x = 0;
    int len = std::min(aChar-1,s.length());
    for (int i=0;i<len;i++) {
        if (s[i] == '\t')
            x+=tabWidth() - (x % tabWidth());
        else
            x+=charColumns(s[i]);
    }
    return x+1;
}

int SynEdit::columnToChar(int aLine, int aColumn) const
{
    Q_ASSERT( (aLine <= mDocument->count()) && (aLine >= 1));
    if (aLine <= mDocument->count()) {
        QString s = getDisplayStringAtLine(aLine);
        int x = 0;
        int len = s.length();
        int i;
        for (i=0;i<len;i++) {
            if (s[i] == '\t')
                x+=tabWidth() - (x % tabWidth());
            else
                x+=charColumns(s[i]);
            if (x>=aColumn) {
                break;
            }
        }
        return i+1;
    }
    return aColumn;
}

int SynEdit::stringColumns(const QString &line, int colsBefore) const
{
    return mDocument->stringColumns(line,colsBefore);
}

int SynEdit::getLineIndent(const QString &line) const
{
    int indents = 0;
    for (QChar ch:line) {
        switch(ch.unicode()) {
        case '\t':
            indents+=tabWidth();
            break;
        case ' ':
            indents+=1;
            break;
        default:
            return indents;
        }
    }
    return indents;
}

int SynEdit::rowToLine(int aRow) const
{
    if (mUseCodeFolding)
        return foldRowToLine(aRow);
    else
        return aRow;
    //return displayToBufferPos({1, aRow}).Line;
}

int SynEdit::lineToRow(int aLine) const
{
    return bufferToDisplayPos({1, aLine}).Row;
}

int SynEdit::foldRowToLine(int Row) const
{
    int result = Row;
    for (int i=0;i<mAllFoldRanges.count();i++) {
        PSynEditFoldRange range = mAllFoldRanges[i];
        if (range->collapsed && !range->parentCollapsed() && range->fromLine < result) {
            result += range->linesCollapsed;
        }
    }
    return result;
}

int SynEdit::foldLineToRow(int Line) const
{
    int result = Line;
    for (int i=mAllFoldRanges.count()-1;i>=0;i--) {
        PSynEditFoldRange range =mAllFoldRanges[i];
        if (range->collapsed && !range->parentCollapsed()) {
            // Line is found after fold
            if (range->toLine < Line)
                result -= range->linesCollapsed;
            // Inside fold
            else if (range->fromLine < Line && Line <= range->toLine)
                result -= Line - range->fromLine;
        }
    }
    return result;
}

void SynEdit::setDefaultKeystrokes()
{
    mKeyStrokes.resetDefaults();
}

void SynEdit::setExtraKeystrokes()
{
    mKeyStrokes.setExtraKeyStrokes();
}

void SynEdit::invalidateLine(int Line)
{
    QRect rcInval;
    if (mPainterLock >0)
        return;
    if (Line<1 || (Line>mDocument->count() &&
                   Line!=1) || !isVisible())
        return;

    // invalidate text area of this line
    if (mUseCodeFolding)
        Line = foldLineToRow(Line);
    if (Line >= mTopLine && Line <= mTopLine + mLinesInWindow) {
        rcInval = { mGutterWidth,
                    mTextHeight * (Line - mTopLine),
                    clientWidth(),
                    mTextHeight};
        if (mStateFlags.testFlag(SynStateFlag::sfLinesChanging))
            mInvalidateRect = mInvalidateRect.united(rcInval);
        else
            invalidateRect(rcInval);
    }
}

void SynEdit::invalidateLines(int FirstLine, int LastLine)
{
    if (mPainterLock>0)
        return;

    if (!isVisible())
        return;
    if (FirstLine == -1 && LastLine == -1) {
        QRect rcInval = clientRect();
        rcInval.setLeft(rcInval.left()+mGutterWidth);
        if (mStateFlags.testFlag(SynStateFlag::sfLinesChanging)) {
            mInvalidateRect = mInvalidateRect.united(rcInval);
        } else {
            invalidateRect(rcInval);
        }
    } else {
        FirstLine = std::max(FirstLine, 1);
        LastLine = std::max(LastLine, 1);
        // find the visible lines first
        if (LastLine < FirstLine)
            std::swap(LastLine, FirstLine);

        if (LastLine >= mDocument->count())
          LastLine = INT_MAX; // paint empty space beyond last line

        if (mUseCodeFolding) {
          FirstLine = lineToRow(FirstLine);
          // Could avoid this conversion if (First = Last) and
          // (Length < CharsInWindow) but the dependency isn't worth IMO.
          if (LastLine < mDocument->count())
              LastLine = lineToRow(LastLine + 1) - 1;
        }

        // mTopLine is in display coordinates, so FirstLine and LastLine must be
        // converted previously.
        FirstLine = std::max(FirstLine, mTopLine);
        LastLine = std::min(LastLine, mTopLine + mLinesInWindow);

        // any line visible?
        if (LastLine >= FirstLine) {
            QRect rcInval = {
                clientLeft()+mGutterWidth,
                mTextHeight * (FirstLine - mTopLine),
                clientWidth(), mTextHeight * (LastLine - mTopLine + 1)
            };
            if (mStateFlags.testFlag(SynStateFlag::sfLinesChanging))
                mInvalidateRect = mInvalidateRect.united(rcInval);
            else
                invalidateRect(rcInval);
        }
    }
}

void SynEdit::invalidateSelection()
{
    if (mPainterLock>0)
        return;
    invalidateLines(blockBegin().Line, blockEnd().Line);
}

void SynEdit::invalidateRect(const QRect &rect)
{
    if (mPainterLock>0)
        return;
    viewport()->update(rect);
}

void SynEdit::invalidate()
{
    if (mPainterLock>0)
        return;
    viewport()->update();
}

void SynEdit::lockPainter()
{
    mPainterLock++;
}

void SynEdit::unlockPainter()
{
    Q_ASSERT(mPainterLock>0);
    mPainterLock--;
}

bool SynEdit::selAvail() const
{
    if (mBlockBegin.Char == mBlockEnd.Char && mBlockBegin.Line == mBlockEnd.Line)
        return false;
    // start line != end line  or start char != end char
    if (mActiveSelectionMode==SynSelectionMode::smColumn) {
        if (mBlockBegin.Line != mBlockEnd.Line) {
            DisplayCoord coordBegin = bufferToDisplayPos(mBlockBegin);
            DisplayCoord coordEnd = bufferToDisplayPos(mBlockEnd);
            return coordBegin.Column!=coordEnd.Column;
        } else
            return true;
    }
    return true;
}

bool SynEdit::colSelAvail() const
{
    if (mActiveSelectionMode != SynSelectionMode::smColumn)
        return false;
    if (mBlockBegin.Char == mBlockEnd.Char && mBlockBegin.Line == mBlockEnd.Line)
        return false;
    if (mBlockBegin.Line == mBlockEnd.Line && mBlockBegin.Char!=mBlockBegin.Char)
        return true;
    DisplayCoord coordBegin = bufferToDisplayPos(mBlockBegin);
    DisplayCoord coordEnd = bufferToDisplayPos(mBlockEnd);
    return coordBegin.Column!=coordEnd.Column;
}

QString SynEdit::wordAtCursor()
{
    return wordAtRowCol(caretXY());
}

QString SynEdit::wordAtRowCol(const BufferCoord &pos)
{
    if ((pos.Line >= 1) && (pos.Line <= mDocument->count())) {
        QString line = mDocument->getString(pos.Line - 1);
        int len = line.length();
        if (len == 0)
            return "";
        if (pos.Char<1 || pos.Char>len)
            return "";

        int start = pos.Char - 1;
        if  ((start> 0) && !isIdentChar(line[start]))
             start--;

        if (isIdentChar(line[start])) {
            int stop = start;
            while ((stop < len) && isIdentChar(line[stop]))
                stop++;
            while ((start-1 >=0) && isIdentChar(line[start - 1]))
                start--;
            if (stop > start)
                return line.mid(start,stop-start);
        }
    }
    return "";
}

QChar SynEdit::charAt(const BufferCoord &pos)
{
    if ((pos.Line >= 1) && (pos.Line <= mDocument->count())) {
        QString line = mDocument->getString(pos.Line-1);
        int len = line.length();
        if (len == 0)
            return QChar(0);
        if (pos.Char<1 || pos.Char>len)
            return QChar(0);
        return line[pos.Char-1];
    }
    return QChar(0);
}

QChar SynEdit::nextNonSpaceChar(int line, int ch)
{
    if (ch<0)
        return QChar();
    QString s = mDocument->getString(line);
    if (s.isEmpty())
        return QChar();
    int x=ch;
    while (x<s.length()) {
        QChar ch = s[x];
        if (!ch.isSpace())
            return ch;
        x++;
    }
    return QChar();
}

QChar SynEdit::lastNonSpaceChar(int line, int ch)
{
    if (line>=mDocument->count())
        return QChar();
    QString s = mDocument->getString(line);
    int x = std::min(ch-1,s.length()-1);
    while (line>=0) {
        while (x>=0) {
            QChar c = s[x];
            if (!c.isSpace())
                return c;
            x--;
        }
        line--;
        if (line>=0) {
            s = mDocument->getString(line);
            x = s.length()-1;
        }
    }
    return QChar();
}

void SynEdit::setCaretAndSelection(const BufferCoord &ptCaret, const BufferCoord &ptSelBegin, const BufferCoord &ptSelEnd)
{
    SynSelectionMode vOldMode = mActiveSelectionMode;
    incPaintLock();
    auto action = finally([this,vOldMode]{
        mActiveSelectionMode = vOldMode;
        decPaintLock();
    });
    internalSetCaretXY(ptCaret);
    setBlockBegin(ptSelBegin);
    setBlockEnd(ptSelEnd);
}

bool SynEdit::inputMethodOn()
{
    return !mInputPreeditString.isEmpty();
}

void SynEdit::collapseAll()
{
    incPaintLock();
    for (int i = mAllFoldRanges.count()-1;i>=0;i--){
        collapse(mAllFoldRanges[i]);
    }
    decPaintLock();
}

void SynEdit::unCollpaseAll()
{
    incPaintLock();
    for (int i = mAllFoldRanges.count()-1;i>=0;i--){
        uncollapse(mAllFoldRanges[i]);
    }
    decPaintLock();
}

void SynEdit::processGutterClick(QMouseEvent *event)
{
    int X = event->pos().x();
    int Y = event->pos().y();
    DisplayCoord RowColumn = pixelsToNearestRowColumn(X, Y);
    int Line = rowToLine(RowColumn.Row);

    // Check if we clicked on a folding thing
    if (mUseCodeFolding) {
        PSynEditFoldRange foldRange = foldStartAtLine(Line);
        if (foldRange) {
            // See if we actually clicked on the rectangle...
            //rect.Left := Gutter.RealGutterWidth(CharWidth) - Gutter.RightOffset;
            QRect rect;
            rect.setLeft(mGutterWidth - mGutter.rightOffset());
            rect.setRight(rect.left() + mGutter.rightOffset() - 4);
            rect.setTop((RowColumn.Row - mTopLine) * mTextHeight);
            rect.setBottom(rect.top() + mTextHeight - 1);
            if (rect.contains(QPoint(X, Y))) {
                if (foldRange->collapsed)
                    uncollapse(foldRange);
                else
                    collapse(foldRange);
                return;
            }
        }
    }

    // If not, check gutter marks
    if (Line>=1 && Line <= mDocument->count()) {
        emit gutterClicked(event->button(),X,Y,Line);
    }
}

void SynEdit::clearUndo()
{
    mUndoList->Clear();
    mRedoList->Clear();
}

int SynEdit::findIndentsStartLine(int line, QVector<int> indents)
{
    line--;
    if (line<0 || line>=mDocument->count())
        return -1;
    while (line>=1) {
        SynRangeState range = mDocument->ranges(line);
        QVector<int> newIndents = range.indents.mid(range.firstIndentThisLine);
        int i = 0;
        int len = indents.length();
        while (i<len && !newIndents.isEmpty()) {
            int indent = indents[i];
            int idx = newIndents.lastIndexOf(indent);
            if (idx >=0) {
                newIndents.remove(idx,newIndents.size());
            } else {
                break;
            }
            i++;
        }
        if (i>=len) {
            return line+1;
        } else {
            indents = range.matchingIndents + indents.mid(i);
        }
        line--;
    }
    return -1;
}

BufferCoord SynEdit::getPreviousLeftBrace(int x, int y)
{
    QChar Test;
    QString vDummy;
    PSynHighlighterAttribute attr;
    BufferCoord p;
    bool isCommentOrStringOrChar;
    BufferCoord Result{0,0};
    // get char at caret
    int PosX = x-1;
    int PosY = y;
    if (PosX<1)
        PosY--;
    if (PosY<1 )
        return Result;
    QString Line = mDocument->getString(PosY - 1);
    if ((PosX > Line.length()) || (PosX<1))
        PosX = Line.length();
    int numBrackets = 1;
    while (true) {
        if (Line.isEmpty()){
            PosY--;
            if (PosY<1)
                return Result;
            Line = mDocument->getString(PosY - 1);
            PosX = Line.length();
            continue;
        }
        Test = Line[PosX-1];
        p.Char = PosX;
        p.Line = PosY;
        if (Test=='{' || Test == '}') {
            if (getHighlighterAttriAtRowCol(p, vDummy, attr)) {
                isCommentOrStringOrChar =
                        (attr == mHighlighter->stringAttribute()) ||
                        (attr == mHighlighter->commentAttribute()) ||
                        (attr->name() == SYNS_AttrCharacter);
            } else
                isCommentOrStringOrChar = false;
            if ((Test == '{') && (! isCommentOrStringOrChar))
                numBrackets--;
            else if ((Test == '}') && (!isCommentOrStringOrChar))
                numBrackets++;
            if (numBrackets == 0) {
                return p;
            }
        }
        PosX--;
        if (PosX<1) {
            PosY--;
            if (PosY<1)
                return Result;
            Line = mDocument->getString(PosY - 1);
            PosX = Line.length();
        }
    }
}

int SynEdit::charColumns(QChar ch) const
{
    return mDocument->charColumns(ch);
}

void SynEdit::showCaret()
{
    if (m_blinkTimerId==0)
        m_blinkTimerId = startTimer(500);
    m_blinkStatus = 1;
    updateCaret();
}

void SynEdit::hideCaret()
{
    if (m_blinkTimerId!=0) {
        killTimer(m_blinkTimerId);
        m_blinkTimerId = 0;
        m_blinkStatus = 0;
        updateCaret();
    }
}

bool SynEdit::isPointInSelection(const BufferCoord &Value) const
{
    BufferCoord ptBegin = blockBegin();
    BufferCoord ptEnd = blockEnd();
    if ((Value.Line >= ptBegin.Line) && (Value.Line <= ptEnd.Line) &&
            ((ptBegin.Line != ptEnd.Line) || (ptBegin.Char != ptEnd.Char))) {
        if (mActiveSelectionMode == SynSelectionMode::smLine)
            return true;
        else if (mActiveSelectionMode == SynSelectionMode::smColumn) {
            if (ptBegin.Char > ptEnd.Char)
                return (Value.Char >= ptEnd.Char) && (Value.Char < ptBegin.Char);
            else if (ptBegin.Char < ptEnd.Char)
                return (Value.Char >= ptBegin.Char) && (Value.Char < ptEnd.Char);
            else
                return false;
        } else
            return ((Value.Line > ptBegin.Line) || (Value.Char >= ptBegin.Char)) &&
      ((Value.Line < ptEnd.Line) || (Value.Char < ptEnd.Char));
    } else
        return false;
}

BufferCoord SynEdit::nextWordPos()
{
    return nextWordPosEx(caretXY());
}

BufferCoord SynEdit::nextWordPosEx(const BufferCoord &XY)
{
    int CX = XY.Char;
    int CY = XY.Line;
    // valid line?
    if ((CY >= 1) && (CY <= mDocument->count())) {
        QString Line = mDocument->getString(CY - 1);
        int LineLen = Line.length();
        if (CX >= LineLen) {
            // find first IdentChar or multibyte char in the next line
            if (CY < mDocument->count()) {
                Line = mDocument->getString(CY);
                CY++;
                CX=StrScanForWordChar(Line,1);
                if (CX==0)
                    CX=1;
            }
        } else {
            // find next "whitespace" if current char is an IdentChar
            if (!Line[CX-1].isSpace())
                CX = StrScanForNonWordChar(Line,CX);
            // if "whitespace" found, find the next IdentChar
            if (CX > 0)
                CX = StrScanForWordChar(Line, CX);
            // if one of those failed position at the begin of next line
            if (CX == 0) {
                if (CY < mDocument->count()) {
                    Line = mDocument->getString(CY);
                    CY++;
                    CX=StrScanForWordChar(Line,1);
                    if (CX==0)
                        CX=1;
                } else {
                    CX=Line.length()+1;
                }
            }
        }
    }
    return BufferCoord{CX,CY};
}

BufferCoord SynEdit::wordStart()
{
    return wordStartEx(caretXY());
}

BufferCoord SynEdit::wordStartEx(const BufferCoord &XY)
{
    int CX = XY.Char;
    int CY = XY.Line;
    // valid line?
    if ((CY >= 1) && (CY <= mDocument->count())) {
        QString Line = mDocument->getString(CY - 1);
        CX = std::min(CX, Line.length()+1);
        if (CX > 1) {
            if (isWordChar(Line[CX - 2]))
                CX = StrRScanForNonWordChar(Line, CX - 1) + 1;
        }
    }
    return BufferCoord{CX,CY};
}

BufferCoord SynEdit::wordEnd()
{
    return wordEndEx(caretXY());
}

BufferCoord SynEdit::wordEndEx(const BufferCoord &XY)
{
    int CX = XY.Char;
    int CY = XY.Line;
    // valid line?
    if ((CY >= 1) && (CY <= mDocument->count())) {
        QString Line = mDocument->getString(CY - 1);
        if (CX <= Line.length() && CX-1>=0) {
            if (isWordChar(Line[CX - 1]))
                CX = StrScanForNonWordChar(Line, CX);
            if (CX == 0)
                CX = Line.length() + 1;
        }
    }
    return BufferCoord{CX,CY};
}

BufferCoord SynEdit::prevWordPos()
{
    return prevWordPosEx(caretXY());
}

BufferCoord SynEdit::prevWordPosEx(const BufferCoord &XY)
{
    int CX = XY.Char;
    int CY = XY.Line;
    // valid line?
    if ((CY >= 1) && (CY <= mDocument->count())) {
        QString Line = mDocument->getString(CY - 1);
        CX = std::min(CX, Line.length());
        if (CX <= 1) {
            // find last IdentChar in the previous line
            if (CY > 1) {
                CY -- ;
                Line = mDocument->getString(CY - 1);
                CX = StrRScanForWordChar(Line, Line.length())+1;
            }
        } else {
            // if previous char is a "whitespace" search for the last IdentChar
            if (!isWordChar(Line[CX - 2]))
                CX = StrRScanForWordChar(Line, CX - 1);
            if (CX > 0) // search for the first IdentChar of this "word"
                CX = StrRScanForNonWordChar(Line, CX - 1)+1;
            if (CX == 0) {
                // find last IdentChar in the previous line
                if (CY > 1) {
                    CY -- ;
                    Line = mDocument->getString(CY - 1);
                    CX = StrRScanForWordChar(Line, Line.length())+1;
                } else {
                    CX = 1;
                }
            }
        }
    }
    return BufferCoord{CX,CY};
}

void SynEdit::setSelWord()
{
    setWordBlock(caretXY());
}

void SynEdit::setWordBlock(BufferCoord Value)
{
//    if (mOptions.testFlag(eoScrollPastEol))
//        Value.Char =
//    else
//        Value.Char = std::max(Value.Char, 1);
    Value.Line = minMax(Value.Line, 1, mDocument->count());
    Value.Char = std::max(Value.Char, 1);
    QString TempString = mDocument->getString(Value.Line - 1); //needed for CaretX = LineLength +1
    if (Value.Char > TempString.length()) {
        internalSetCaretXY(BufferCoord{TempString.length()+1, Value.Line});
        return;
    }

    BufferCoord v_WordStart = wordStartEx(Value);
    BufferCoord v_WordEnd = wordEndEx(Value);
    if ((v_WordStart.Line == v_WordEnd.Line) && (v_WordStart.Char < v_WordEnd.Char))
        setCaretAndSelection(v_WordEnd, v_WordStart, v_WordEnd);
}

int SynEdit::findCommentStartLine(int searchStartLine)
{
    int commentStartLine = searchStartLine;
    SynRangeState range;
    while (commentStartLine>=1) {
        range = mDocument->ranges(commentStartLine-1);
        if (!mHighlighter->isLastLineCommentNotFinished(range.state)){
            commentStartLine++;
            break;
        }
        if (!range.matchingIndents.isEmpty()
                || range.firstIndentThisLine<range.indents.length())
            break;
        commentStartLine--;
    }
    if (commentStartLine<1)
        commentStartLine = 1;
    return commentStartLine;
}

int SynEdit::calcIndentSpaces(int line, const QString& lineText, bool addIndent)
{
    if (!mHighlighter)
        return 0;
    line = std::min(line, mDocument->count()+1);
    if (line<=1)
        return 0;
    // find the first non-empty preceeding line
    int startLine = line-1;
    QString startLineText;
    while (startLine>=1) {
        startLineText = mDocument->getString(startLine-1);
        if (!startLineText.startsWith('#') && !startLineText.trimmed().isEmpty()) {
            break;
        }
        startLine -- ;
    }
    int indentSpaces = 0;
    if (startLine>=1) {
        //calculate the indents of last statement;
        indentSpaces = leftSpaces(startLineText);
        SynRangeState rangePreceeding = mDocument->ranges(startLine-1);
        mHighlighter->setState(rangePreceeding);
        if (addIndent) {
//            QString trimmedS = s.trimmed();
            QString trimmedLineText = lineText.trimmed();
            mHighlighter->setLine(trimmedLineText,line-1);
            int statePrePre;
            if (startLine>1) {
                statePrePre = mDocument->ranges(startLine-2).state;
            } else {
                statePrePre = 0;
            }
            SynRangeState rangeAfterFirstToken = mHighlighter->getRangeState();
            QString firstToken = mHighlighter->getToken();
            PSynHighlighterAttribute attr = mHighlighter->getTokenAttribute();
            if (attr == mHighlighter->keywordAttribute()
                                  &&  lineText.endsWith(':')
                                  && (
                                  firstToken == "public" || firstToken == "private"
                                  || firstToken == "protected" || firstToken == "case")) {
                // public: private: protecte: case: should indents like it's parent statement
                mHighlighter->setState(rangePreceeding);
                mHighlighter->setLine("}",line-1);
                rangeAfterFirstToken = mHighlighter->getRangeState();
                firstToken = mHighlighter->getToken();
                attr = mHighlighter->getTokenAttribute();
            }
            bool indentAdded = false;
            int additionIndent = 0;
            QVector<int> matchingIndents;
            int l;
            if (attr == mHighlighter->symbolAttribute()
                    && (firstToken == '}')) {
                // current line starts with '}', we should consider it to calc indents
                matchingIndents = rangeAfterFirstToken.matchingIndents;
                indentAdded = true;
                l = startLine;
            } else if (attr == mHighlighter->symbolAttribute()
                       && (firstToken == '{')
                       && (rangePreceeding.getLastIndent()==sitStatement)) {
                // current line starts with '{' and last statement not finished, we should consider it to calc indents
                matchingIndents = rangeAfterFirstToken.matchingIndents;
                indentAdded = true;
                l = startLine;
            } else if (mHighlighter->getClass() == SynHighlighterClass::CppHighlighter
                       && trimmedLineText.startsWith('#')
                       && attr == ((SynEditCppHighlighter *)mHighlighter.get())->preprocessorAttribute()) {
                indentAdded = true;
                indentSpaces=0;
                l=0;
            } else if (mHighlighter->getClass() == SynHighlighterClass::CppHighlighter
                       && mHighlighter->isLastLineCommentNotFinished(rangePreceeding.state)
                       ) {
                // last line is a not finished comment,
                if  (trimmedLineText.startsWith("*")) {
                    // this line start with "* "
                    // it means this line is a docstring, should indents according to
                    // the line the comment beginning , and add 1 additional space
                    additionIndent = 1;
                    int commentStartLine = findCommentStartLine(startLine-1);
                    SynRangeState range;
                    indentSpaces = leftSpaces(mDocument->getString(commentStartLine-1));
                    range = mDocument->ranges(commentStartLine-1);
                    matchingIndents = range.matchingIndents;
                    indentAdded = true;
                    l = commentStartLine;
                } else {
                    //indents according to the beginning of the comment and 2 additional space
                    additionIndent = 0;
                    int commentStartLine = findCommentStartLine(startLine-1);
                    SynRangeState range;
                    indentSpaces = leftSpaces(mDocument->getString(commentStartLine-1))+2;
                    range = mDocument->ranges(commentStartLine-1);
                    matchingIndents = range.matchingIndents;
                    indentAdded = true;
                    l = startLine;
                }
            } else if ( mHighlighter->isLastLineCommentNotFinished(statePrePre)
                        && rangePreceeding.matchingIndents.isEmpty()
                        && rangePreceeding.firstIndentThisLine>=rangePreceeding.indents.length()
                        && !mHighlighter->isLastLineCommentNotFinished(rangePreceeding.state)) {
                // the preceeding line is the end of comment
                // we should use the indents of the start line of the comment
                int commentStartLine = findCommentStartLine(startLine-2);
                SynRangeState range;
                indentSpaces = leftSpaces(mDocument->getString(commentStartLine-1));
                range = mDocument->ranges(commentStartLine-1);
                matchingIndents = range.matchingIndents;
                indentAdded = true;
                l = commentStartLine;
            } else {
                // we just use infos till preceeding line's end to calc indents
                matchingIndents = rangePreceeding.matchingIndents;
                l = startLine-1;
            }

            if (!matchingIndents.isEmpty()
                    ) {
                // find the indent's start line, and use it's indent as the default indent;
                while (l>=1) {
                    SynRangeState range = mDocument->ranges(l-1);
                    QVector<int> newIndents = range.indents.mid(range.firstIndentThisLine);
                    int i = 0;
                    int len = matchingIndents.length();
                    while (i<len && !newIndents.isEmpty()) {
                        int indent = matchingIndents[i];
                        int idx = newIndents.lastIndexOf(indent);
                        if (idx >=0) {
                            newIndents.remove(idx,newIndents.length()-idx);
                        } else {
                            break;
                        }
                        i++;
                    }
                    if (i>=len) {
                        // we found the where the indent started
                        if (len>0 && !range.matchingIndents.isEmpty()
                                &&
                                ( matchingIndents.back()== sitBrace
                                  || matchingIndents.back() == sitStatement
                                ) ) {
                            // but it's not a complete statement
                            matchingIndents = range.matchingIndents;
                        } else {
                            indentSpaces = leftSpaces(mDocument->getString(l-1));
                            if (newIndents.length()>0)
                                indentSpaces+=tabWidth();
                            break;
                        }
                    } else {
                        matchingIndents = range.matchingIndents + matchingIndents.mid(i);
                    }
                    l--;
                }
            }
            if (!indentAdded) {
                if (rangePreceeding.firstIndentThisLine < rangePreceeding.indents.length()) {
                    indentSpaces += tabWidth();
                    indentAdded = true;
                }
            }

            if (!indentAdded && !startLineText.isEmpty()) {
                BufferCoord coord;
                QString token;
                PSynHighlighterAttribute attr;
                coord.Line = startLine;
                coord.Char = document()->getString(startLine-1).length();
                if (getHighlighterAttriAtRowCol(coord,token,attr)
                        && attr == mHighlighter->symbolAttribute()
                        && token == ":") {
                    indentSpaces += tabWidth();
                    indentAdded = true;
                }
            }
            indentSpaces += additionIndent;
        }
    }
    return std::max(0,indentSpaces);
}

void SynEdit::doSelectAll()
{
    BufferCoord LastPt;
    LastPt.Char = 1;
    if (mDocument->empty()) {
        LastPt.Line = 1;
    } else {
        LastPt.Line = mDocument->count();
        LastPt.Char = mDocument->getString(LastPt.Line-1).length()+1;
    }
    setCaretAndSelection(caretXY(), BufferCoord{1, 1}, LastPt);
    // Selection should have changed...
    emit statusChanged(SynStatusChange::scSelection);
}

void SynEdit::doComment()
{
    BufferCoord origBlockBegin, origBlockEnd, origCaret;
    int endLine;
    if (mReadOnly)
        return;
    doOnPaintTransient(SynTransientType::ttBefore);
    mUndoList->BeginBlock();
    auto action = finally([this]{
        mUndoList->EndBlock();
    });
    origBlockBegin = blockBegin();
    origBlockEnd = blockEnd();
    origCaret = caretXY();
    // Ignore the last line the cursor is placed on
    if (origBlockEnd.Char == 1)
        endLine = std::max(origBlockBegin.Line - 1, origBlockEnd.Line - 2);
    else
        endLine = origBlockEnd.Line - 1;
    for (int i = origBlockBegin.Line - 1; i<=endLine; i++) {
        mDocument->putString(i, "//" + mDocument->getString(i));
        mUndoList->AddChange(SynChangeReason::crInsert,
              BufferCoord{1, i + 1},
              BufferCoord{3, i + 1},
              QStringList(), SynSelectionMode::smNormal);
    }
    // When grouping similar commands, process one comment action per undo/redo
    mUndoList->AddGroupBreak();
    // Move begin of selection
    if (origBlockBegin.Char > 1)
        origBlockBegin.Char+=2;
    // Move end of selection
    if (origBlockEnd.Char > 1)
        origBlockEnd.Char+=2;
    // Move caret
    if (origCaret.Char > 1)
          origCaret.Char+=2;
    setCaretAndSelection(origCaret, origBlockBegin, origBlockEnd);
}

void SynEdit::doUncomment()
{
    BufferCoord origBlockBegin, origBlockEnd, origCaret;
    int endLine;
    QString s;
    QStringList changeText;
    changeText.append("//");
    if (mReadOnly)
        return;
    doOnPaintTransient(SynTransientType::ttBefore);
    mUndoList->BeginBlock();
    auto action = finally([this]{
        mUndoList->EndBlock();
    });
    origBlockBegin = blockBegin();
    origBlockEnd = blockEnd();
    origCaret = caretXY();
    // Ignore the last line the cursor is placed on
    if (origBlockEnd.Char == 1)
        endLine = std::max(origBlockBegin.Line - 1, origBlockEnd.Line - 2);
    else
        endLine = origBlockEnd.Line - 1;
    for (int i = origBlockBegin.Line - 1; i<= endLine; i++) {
        s = mDocument->getString(i);
        // Find // after blanks only
        int j = 0;
        while ((j+1 < s.length()) && (s[j] == '\n' || s[j] == '\t'))
            j++;
        if ((j + 1 < s.length()) && (s[j] == '/') && (s[j + 1] == '/')) {
            s.remove(j,2);
            mDocument->putString(i,s);
            mUndoList->AddChange(SynChangeReason::crDelete,
                                 BufferCoord{j+1, i + 1},
                                 BufferCoord{j + 3, i + 1},
                                 changeText, SynSelectionMode::smNormal);
            // Move begin of selection
            if ((i == origBlockBegin.Line - 1) && (origBlockBegin.Char > 1))
                origBlockBegin.Char-=2;
            // Move end of selection
            if ((i == origBlockEnd.Line - 1) && (origBlockEnd.Char > 1))
                origBlockEnd.Char-=2;
            // Move caret
            if ((i == origCaret.Line - 1) && (origCaret.Char > 1))
                origCaret.Char-=2;
        }
    }
    // When grouping similar commands, process one uncomment action per undo/redo
    mUndoList->AddGroupBreak();
    setCaretAndSelection(origCaret,origBlockBegin,origBlockEnd);
}

void SynEdit::doToggleComment()
{
    BufferCoord origBlockBegin, origBlockEnd, origCaret;
    int endLine;
    QString s;
    bool allCommented = true;
    if (mReadOnly)
        return;
    doOnPaintTransient(SynTransientType::ttBefore);
    mUndoList->BeginBlock();
    auto action = finally([this]{
        mUndoList->EndBlock();
    });
    origBlockBegin = blockBegin();
    origBlockEnd = blockEnd();
    origCaret = caretXY();
    // Ignore the last line the cursor is placed on
    if (origBlockEnd.Char == 1)
        endLine = std::max(origBlockBegin.Line - 1, origBlockEnd.Line - 2);
    else
        endLine = origBlockEnd.Line - 1;
    for (int i = origBlockBegin.Line - 1; i<= endLine; i++) {
        s = mDocument->getString(i);
        // Find // after blanks only
        int j = 0;
        while ((j < s.length()) && (s[j] == '\n' || s[j] == '\t'))
            j++;
        if (j>= s.length())
            continue;
        if (s[j] != '/'){
            allCommented = false;
            break;
        }
        if (j+1>=s.length()) {
            allCommented = false;
            break;
        }
        if (s[j + 1] != '/') {
            allCommented = false;
            break;
        }
    }
    if (allCommented)
        doUncomment();
    else
        doComment();
}

void SynEdit::doToggleBlockComment()
{
    QString s;
    if (mReadOnly)
        return;
    doOnPaintTransient(SynTransientType::ttBefore);

    QString text=selText().trimmed();
    if (text.length()>4 && text.startsWith("/*") && text.endsWith("*/")) {
        QString newText=selText();
        int pos = newText.indexOf("/*");
        if (pos>=0) {
            newText.remove(pos,2);
        }
        pos = newText.lastIndexOf("*/");
        if (pos>=0) {
            newText.remove(pos,2);
        }
        setSelText(newText);
    } else {
        QString newText="/*"+selText()+"*/";
        setSelText(newText);
    }

}

void SynEdit::doMouseScroll(bool isDragging)
{
    if (mDropped) {
        mDropped=false;
        return;
    }
    if (!hasFocus())
        return;
    Qt::MouseButtons buttons = qApp->mouseButtons();
    if (!buttons.testFlag(Qt::LeftButton))
        return;
    QPoint iMousePos;
    DisplayCoord C;
    int X, Y;

    iMousePos = QCursor::pos();
    iMousePos = mapFromGlobal(iMousePos);
    C = pixelsToNearestRowColumn(iMousePos.x(), iMousePos.y());
    C.Row = minMax(C.Row, 1, displayLineCount());
    if (mScrollDeltaX != 0) {
        setLeftChar(leftChar() + mScrollDeltaX * mMouseSelectionScrollSpeed);
        X = leftChar();
        if (mScrollDeltaX > 0) // scrolling right?
            X+=charsInWindow();
        C.Column = X;
    }
    if (mScrollDeltaY != 0) {
        //qDebug()<<mScrollDeltaY;
        if (QApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier))
          setTopLine(mTopLine + mScrollDeltaY * mLinesInWindow);
        else
          setTopLine(mTopLine + mScrollDeltaY * mMouseSelectionScrollSpeed);
        Y = mTopLine;
        if (mScrollDeltaY > 0)  // scrolling down?
            Y+=mLinesInWindow - 1;
        C.Row = minMax(Y, 1, displayLineCount());
    }
    BufferCoord vCaret = displayToBufferPos(C);
    if ((caretX() != vCaret.Char) || (caretY() != vCaret.Line)) {
        if (mActiveSelectionMode == SynSelectionMode::smColumn) {
            int startLine=std::min(mBlockBegin.Line,mBlockEnd.Line);
            startLine = std::min(startLine,vCaret.Line);
            int endLine=std::max(mBlockBegin.Line,mBlockEnd.Line);
            endLine = std::max(endLine,vCaret.Line);

            int currentCol=displayXY().Column;
            for (int i=startLine;i<=endLine;i++) {
                QString s = mDocument->getString(i-1);
                int cols = stringColumns(s,0);
                if (cols+1<currentCol) {
                    computeScroll(isDragging);
                    return;
                }
            }

        }
        // changes to line / column in one go
        incPaintLock();
        auto action = finally([this]{
            decPaintLock();
        });
        internalSetCaretXY(vCaret);

        // if MouseCapture is True we're changing selection. otherwise we're dragging
        if (isDragging) {
            setBlockBegin(mDragSelBeginSave);
            setBlockEnd(mDragSelEndSave);
        } else
            setBlockEnd(caretXY());
    }
    computeScroll(isDragging);
}

QString SynEdit::getDisplayStringAtLine(int line) const
{
    QString s = mDocument->getString(line-1);
    PSynEditFoldRange foldRange = foldStartAtLine(line);
    if ((foldRange) && foldRange->collapsed) {
        return s+highlighter()->foldString();
    }
    return s;
}

void SynEdit::doDeleteLastChar()
{
    if (mReadOnly)
        return ;
    doOnPaintTransientEx(SynTransientType::ttBefore, true);
    auto action = finally([this]{
        ensureCursorPosVisible();
        doOnPaintTransientEx(SynTransientType::ttAfter, true);
    });

    if (mActiveSelectionMode==SynSelectionMode::smColumn) {
        BufferCoord start=blockBegin();
        BufferCoord end=blockEnd();
        if (!selAvail()) {
            start.Char--;
            setBlockBegin(start);
            setBlockEnd(end);
        }
        setSelectedTextEmpty();
        return;
    }
    if (selAvail()) {
        setSelectedTextEmpty();
        return;
    }
    bool shouldAddGroupBreak=false;
    QString Temp = lineText();
    int Len = Temp.length();
    BufferCoord Caret = caretXY();
    QStringList helper;
    if (mCaretX > Len + 1) {
        // only move caret one column
        return;
    } else if (mCaretX == 1) {
        // join this line with the last line if possible
        if (mCaretY > 1) {
            internalSetCaretY(mCaretY - 1);
            internalSetCaretX(mDocument->getString(mCaretY - 1).length() + 1);
            mDocument->deleteAt(mCaretY);
            doLinesDeleted(mCaretY+1, 1);
            if (mOptions.testFlag(eoTrimTrailingSpaces))
                Temp = trimRight(Temp);
            setLineText(lineText() + Temp);
            helper.append("");
            helper.append("");
            shouldAddGroupBreak=true;
        }
    } else {
        // delete text before the caret
        int caretColumn = charToColumn(mCaretY,mCaretX);
        int SpaceCount1 = leftSpaces(Temp);
        int SpaceCount2 = 0;
        int newCaretX;

        if (SpaceCount1 == caretColumn - 1) {
                //how much till the next tab column
                int BackCounter = (caretColumn - 1) % tabWidth();
                if (BackCounter == 0)
                    BackCounter = tabWidth();
                SpaceCount2 = std::max(0,SpaceCount1 - tabWidth());
                newCaretX = columnToChar(mCaretY,SpaceCount2+1);
                helper.append(Temp.mid(newCaretX - 1, mCaretX - newCaretX));
                Temp.remove(newCaretX-1,mCaretX - newCaretX);
            properSetLine(mCaretY - 1, Temp);
            internalSetCaretX(newCaretX);
        } else {
            // delete char
            internalSetCaretX(mCaretX - 1);
            QChar ch=Temp[mCaretX-1];
            if (ch==' ' || ch=='\t')
                shouldAddGroupBreak=true;
            helper.append(QString(ch));
            Temp.remove(mCaretX-1,1);
            properSetLine(mCaretY - 1, Temp);
        }
    }
    if ((Caret.Char != mCaretX) || (Caret.Line != mCaretY)) {
        mUndoList->AddChange(SynChangeReason::crDelete, caretXY(), Caret, helper,
                        mActiveSelectionMode);
        if (shouldAddGroupBreak)
            mUndoList->AddGroupBreak();
    }
}

void SynEdit::doDeleteCurrentChar()
{
    QStringList helper;
    BufferCoord Caret;
    if (mReadOnly) {
        return;
    }
    doOnPaintTransient(SynTransientType::ttBefore);
    auto action = finally([this]{
        ensureCursorPosVisible();
        doOnPaintTransient(SynTransientType::ttAfter);
    });

    if (mActiveSelectionMode==SynSelectionMode::smColumn) {
        BufferCoord start=blockBegin();
        BufferCoord end=blockEnd();
        if (!selAvail()) {
            end.Char++;
            setBlockBegin(start);
            setBlockEnd(end);
        }
        setSelectedTextEmpty();
        return;
    }
    if (selAvail())
        setSelectedTextEmpty();
    else {
        bool shouldAddGroupBreak=false;
        // Call UpdateLastCaretX. Even though the caret doesn't move, the
        // current caret position should "stick" whenever text is modified.
        updateLastCaretX();
        QString Temp = lineText();
        int Len = Temp.length();
        if (mCaretX>Len+1) {
            return;
        } else if (mCaretX <= Len) {
            QChar ch = Temp[mCaretX-1];
            if (ch==' ' || ch=='\t')
                shouldAddGroupBreak=true;
            // delete char
            helper.append(QString(ch));
            Caret.Char = mCaretX + 1;
            Caret.Line = mCaretY;
            Temp.remove(mCaretX-1, 1);
            properSetLine(mCaretY - 1, Temp);
        } else {
            // join line with the line after
            if (mCaretY < mDocument->count()) {
                shouldAddGroupBreak=true;
                properSetLine(mCaretY - 1, Temp + mDocument->getString(mCaretY));
                Caret.Char = 1;
                Caret.Line = mCaretY + 1;
                helper.append("");
                helper.append("");
                mDocument->deleteAt(mCaretY);
                if (mCaretX==1)
                    doLinesDeleted(mCaretY, 1);
                else
                    doLinesDeleted(mCaretY + 1, 1);
            }
        }
        if ((Caret.Char != mCaretX) || (Caret.Line != mCaretY)) {
            mUndoList->AddChange(SynChangeReason::crDelete, caretXY(), Caret,
                  helper, mActiveSelectionMode);
            if (shouldAddGroupBreak)
                mUndoList->AddGroupBreak();
        }
    }
}

void SynEdit::doDeleteWord()
{
    if (mReadOnly)
        return;
    if (mCaretX>lineText().length()+1)
        return;

    BufferCoord start = wordStart();
    BufferCoord end = wordEnd();
    deleteFromTo(start,end);
}

void SynEdit::doDeleteToEOL()
{
    if (mReadOnly)
        return;
    if (mCaretX>lineText().length()+1)
        return;

    deleteFromTo(caretXY(),BufferCoord{lineText().length()+1,mCaretY});
}

void SynEdit::doDeleteToWordStart()
{
    if (mReadOnly)
        return;
    if (mCaretX>lineText().length()+1)
        return;

    BufferCoord start = wordStart();
    BufferCoord end = caretXY();
    if (start==end) {
        start = prevWordPos();
    }
    deleteFromTo(start,end);
}

void SynEdit::doDeleteToWordEnd()
{
    if (mReadOnly)
        return;
    if (mCaretX>lineText().length()+1)
        return;

    BufferCoord start = caretXY();
    BufferCoord end = wordEnd();
    if (start == end) {
        end = wordEndEx(nextWordPos());
    }
    deleteFromTo(start,end);
}

void SynEdit::doDeleteFromBOL()
{
    if (mReadOnly)
        return;
    if (mCaretX>lineText().length()+1)
        return;

    deleteFromTo(BufferCoord{1,mCaretY},caretXY());
}

void SynEdit::doDeleteLine()
{
    if (!mReadOnly && (mDocument->count() > 0)) {
        PSynEditFoldRange foldRange=foldStartAtLine(mCaretY);
        if (foldRange && foldRange->collapsed)
            return;
        doOnPaintTransient(SynTransientType::ttBefore);
        mUndoList->BeginBlock();
        mUndoList->AddChange(SynChangeReason::crCaret,
                             caretXY(),
                             caretXY(),
                             QStringList(),
                             mActiveSelectionMode);
        mUndoList->AddChange(SynChangeReason::crSelection,
                             mBlockBegin,
                             mBlockEnd,
                             QStringList(),
                             mActiveSelectionMode);
        if (selAvail())
            setBlockBegin(caretXY());
        QStringList helper(lineText());
        if (mCaretY == mDocument->count()) {
            if (mDocument->count()==1) {
                mDocument->putString(mCaretY - 1,"");
                mUndoList->AddChange(SynChangeReason::crDelete,
                                     BufferCoord{1, mCaretY},
                                     BufferCoord{helper.length() + 1, mCaretY},
                                     helper, SynSelectionMode::smNormal);
            } else {
                QString s = mDocument->getString(mCaretY-2);
                mDocument->deleteAt(mCaretY - 1);
                helper.insert(0,"");
                mUndoList->AddChange(SynChangeReason::crDelete,
                                     BufferCoord{s.length()+1, mCaretY-1},
                                     BufferCoord{helper.length() + 1, mCaretY},
                                     helper, SynSelectionMode::smNormal);
                doLinesDeleted(mCaretY, 1);
                mCaretY--;
            }
        } else {
            mDocument->deleteAt(mCaretY - 1);
            helper.append("");
            mUndoList->AddChange(SynChangeReason::crDelete,
                                 BufferCoord{1, mCaretY},
                                 BufferCoord{helper.length() + 1, mCaretY},
                                 helper, SynSelectionMode::smNormal);
            doLinesDeleted(mCaretY, 1);
        }
        mUndoList->EndBlock();
        internalSetCaretXY(BufferCoord{1, mCaretY}); // like seen in the Delphi editor
        doOnPaintTransient(SynTransientType::ttAfter);
    }
}

void SynEdit::doSelecteLine()
{
    setBlockBegin(BufferCoord{1,mCaretY});
    if (mCaretY==mDocument->count())
        setBlockEnd(BufferCoord{lineText().length()+1,mCaretY});
    else
        setBlockEnd(BufferCoord{1,mCaretY+1});
}

void SynEdit::doDuplicateLine()
{
    if (!mReadOnly && (mDocument->count() > 0)) {
        PSynEditFoldRange foldRange=foldStartAtLine(mCaretY);
        if (foldRange && foldRange->collapsed)
            return;
        QString s = lineText();
        doOnPaintTransient(SynTransientType::ttBefore);
        mDocument->insert(mCaretY, lineText());
        doLinesInserted(mCaretY + 1, 1);
        mUndoList->BeginBlock();
        mUndoList->AddChange(SynChangeReason::crCaret,
                             caretXY(),caretXY(),QStringList(),SynSelectionMode::smNormal);
        mUndoList->AddChange(SynChangeReason::crLineBreak,
                             BufferCoord{s.length()+1,mCaretY},
                             BufferCoord{s.length()+1,mCaretY}, QStringList(), SynSelectionMode::smNormal);
        mUndoList->AddChange(SynChangeReason::crInsert,
                             BufferCoord{1,mCaretY+1},
                             BufferCoord{s.length()+1,mCaretY+1}, QStringList(), SynSelectionMode::smNormal);
        mUndoList->EndBlock();
        internalSetCaretXY(BufferCoord{1, mCaretY}); // like seen in the Delphi editor
        doOnPaintTransient(SynTransientType::ttAfter);
    }
}

void SynEdit::doMoveSelUp()
{
    if (mActiveSelectionMode == SynSelectionMode::smColumn)
        return;
    if (!mReadOnly && (mDocument->count() > 0) && (blockBegin().Line > 1)) {
        BufferCoord origBlockBegin = blockBegin();
        BufferCoord origBlockEnd = blockEnd();
        PSynEditFoldRange foldRange=foldStartAtLine(origBlockEnd.Line);
        if (foldRange && foldRange->collapsed)
            return;
//        for (int line=origBlockBegin.Line;line<=origBlockEnd.Line;line++) {
//            PSynEditFoldRange foldRange=foldStartAtLine(line);
//            if (foldRange && foldRange->collapsed)
//                return;
//        }

        doOnPaintTransient(SynTransientType::ttBefore);

        // Backup caret and selection

        if (!mUndoing) {
            mUndoList->BeginBlock();
            mUndoList->AddChange(SynChangeReason::crCaret, // backup original caret
                    caretXY(),
                    caretXY(),
                    QStringList(),
                    SynSelectionMode::smNormal);
            mUndoList->AddChange(SynChangeReason::crMoveSelectionUp,
                    origBlockBegin,
                    origBlockEnd,
                    QStringList(),
                    SynSelectionMode::smNormal);
            mUndoList->EndBlock();
        }
        // Delete line above selection
        QString s = mDocument->getString(origBlockBegin.Line - 2); // before start, 0 based
        mDocument->deleteAt(origBlockBegin.Line - 2); // before start, 0 based
        doLinesDeleted(origBlockBegin.Line - 1, 1); // before start, 1 based

        // Insert line below selection
        mDocument->insert(origBlockEnd.Line - 1, s);
        doLinesInserted(origBlockEnd.Line, 1);
        // Restore caret and selection
        setCaretAndSelection(
                  BufferCoord{mCaretX, origBlockBegin.Line - 1},
                  BufferCoord{origBlockBegin.Char, origBlockBegin.Line - 1},
                  BufferCoord{origBlockEnd.Char, origBlockEnd.Line - 1}
        );

        doOnPaintTransient(SynTransientType::ttAfter);
    }
}

void SynEdit::doMoveSelDown()
{
    if (mActiveSelectionMode == SynSelectionMode::smColumn)
        return;
    if (!mReadOnly && (mDocument->count() > 0) && (blockEnd().Line < mDocument->count())) {
        BufferCoord origBlockBegin = blockBegin();
        BufferCoord origBlockEnd = blockEnd();
        PSynEditFoldRange foldRange=foldStartAtLine(origBlockEnd.Line);
        if (foldRange && foldRange->collapsed)
            return;
//        for (int line=origBlockBegin.Line;line<=origBlockEnd.Line;line++) {
//            PSynEditFoldRange foldRange=foldStartAtLine(line.Line);
//            if (foldRange && foldRange->collapsed)
//                return;
//        }
        doOnPaintTransient(SynTransientType::ttBefore);
        // Backup caret and selection
        if (!mUndoing) {
            mUndoList->BeginBlock();
            mUndoList->AddChange(SynChangeReason::crCaret, // backup original caret
                    caretXY(),
                    caretXY(),
                    QStringList(),
                    SynSelectionMode::smNormal);
            mUndoList->AddChange(SynChangeReason::crMoveSelectionDown,
                    origBlockBegin,
                    origBlockEnd,
                    QStringList(),
                    SynSelectionMode::smNormal);
            mUndoList->EndBlock();
        }

        // Delete line below selection
        QString s = mDocument->getString(origBlockEnd.Line); // after end, 0 based
        mDocument->deleteAt(origBlockEnd.Line); // after end, 0 based
        doLinesDeleted(origBlockEnd.Line, 1); // before start, 1 based

        // Insert line above selection
        mDocument->insert(origBlockBegin.Line - 1, s);
        doLinesInserted(origBlockBegin.Line, 1);

        // Restore caret and selection
        setCaretAndSelection(
                  BufferCoord{mCaretX, origBlockEnd.Line + 1},
                  BufferCoord{origBlockBegin.Char, origBlockBegin.Line + 1},
                  BufferCoord{origBlockEnd.Char, origBlockEnd.Line + 1}
                    );

        // Retrieve start of line we moved down
        doOnPaintTransient(SynTransientType::ttAfter);
    }
}

void SynEdit::clearAll()
{
    mDocument->clear();
    mMarkList.clear();
    mUndoList->Clear();
    mRedoList->Clear();
    setModified(false);
}

void SynEdit::insertLine(bool moveCaret)
{
    if (mReadOnly)
        return;
    int nLinesInserted=0;
    if (!mUndoing)
        mUndoList->BeginBlock();
    auto action = finally([this] {
        if (!mUndoing)
            mUndoList->EndBlock();
    });
    QString helper;
    if (selAvail()) {
        helper = selText();
        setSelectedTextEmpty();
    }

    QString Temp = lineText();

    if (mCaretX>lineText().length()+1) {
        PSynEditFoldRange foldRange = foldStartAtLine(mCaretY);
        if ((foldRange) && foldRange->collapsed) {
            QString s = Temp+highlighter()->foldString();
            if (mCaretX > s.length()) {
                if (!mUndoing) {
                    addCaretToUndo();
                    addSelectionToUndo();
                }
                mCaretY=foldRange->toLine;
                if (mCaretY>mDocument->count()) {
                    mCaretY=mDocument->count();
                }
                Temp = lineText();
                mCaretX=Temp.length()+1;
            }
        }
    }

    QString Temp2 = Temp;
    QString Temp3;
    PSynHighlighterAttribute Attr;

    // This is sloppy, but the Right Thing would be to track the column of markers
    // too, so they could be moved depending on whether they are after the caret...
    int InsDelta = (mCaretX == 1)?1:0;
    QString leftLineText = lineText().mid(0, mCaretX - 1);
    QString rightLineText = lineText().mid(mCaretX-1);
    bool notInComment=true;
    properSetLine(mCaretY-1,leftLineText);
    //update range stated for line mCaretY
    if (mHighlighter) {
        if (mCaretY==1) {
            mHighlighter->resetState();
        } else {
            mHighlighter->setState(mDocument->ranges(mCaretY-2));
        }
        mHighlighter->setLine(leftLineText, mCaretY-1);
        mHighlighter->nextToEol();
        mDocument->setRange(mCaretY-1,mHighlighter->getRangeState());
        notInComment = !mHighlighter->isLastLineCommentNotFinished(
                    mHighlighter->getRangeState().state)
                && !mHighlighter->isLastLineStringNotFinished(
                    mHighlighter->getRangeState().state);
    }
    int indentSpaces = 0;
    if (mOptions.testFlag(eoAutoIndent)) {
        rightLineText=trimLeft(rightLineText);
        indentSpaces = calcIndentSpaces(mCaretY+1,
                                        rightLineText,mOptions.testFlag(eoAutoIndent)
                                            );
    }
    QString indentSpacesForRightLineText = GetLeftSpacing(indentSpaces,true);
    mDocument->insert(mCaretY, indentSpacesForRightLineText+rightLineText);
    nLinesInserted++;
    if (!mUndoing)
        mUndoList->AddChange(SynChangeReason::crLineBreak, caretXY(), caretXY(), QStringList(rightLineText),
              SynSelectionMode::smNormal);

    if (!mUndoing) {
        //insert new line in middle of "/*" and "*/"
        if (!notInComment &&
                ( leftLineText.endsWith("/*") && rightLineText.startsWith("*/")
                 )) {
            indentSpaces = calcIndentSpaces(mCaretY+1, "" , mOptions.testFlag(eoAutoIndent));
            indentSpacesForRightLineText = GetLeftSpacing(indentSpaces,true);
            mDocument->insert(mCaretY, indentSpacesForRightLineText);
            nLinesInserted++;
            mUndoList->AddChange(SynChangeReason::crLineBreak, caretXY(), caretXY(), QStringList(),
                    SynSelectionMode::smNormal);
        }
        //insert new line in middle of "{" and "}"
        if (notInComment &&
                ( leftLineText.endsWith('{') && rightLineText.startsWith('}')
                 )) {
            indentSpaces = calcIndentSpaces(mCaretY+1, "" , mOptions.testFlag(eoAutoIndent)
                                                                   && notInComment);
            indentSpacesForRightLineText = GetLeftSpacing(indentSpaces,true);
            mDocument->insert(mCaretY, indentSpacesForRightLineText);
            nLinesInserted++;
            mUndoList->AddChange(SynChangeReason::crLineBreak, caretXY(), caretXY(), QStringList(),
                    SynSelectionMode::smNormal);
        }
    }
    if (moveCaret)
        internalSetCaretXY(BufferCoord{indentSpacesForRightLineText.length()+1,mCaretY + 1});

    doLinesInserted(mCaretY - InsDelta, nLinesInserted);
    setBlockBegin(caretXY());
    setBlockEnd(caretXY());
    ensureCursorPosVisible();
    updateLastCaretX();
}

void SynEdit::doTabKey()
{
    if (mActiveSelectionMode == SynSelectionMode::smColumn) {
        doAddChar('\t');
        return;
    }
    // Provide Visual Studio like block indenting
    if (mOptions.testFlag(eoTabIndent) && canDoBlockIndent()) {
        doBlockIndent();
        return;
    }
    int i = 0;
    {
        mUndoList->BeginBlock();
        auto action = finally([this]{
            mUndoList->EndBlock();
        });
        if (selAvail()) {
            setSelectedTextEmpty();
        }
        QString Spaces;
        int NewCaretX = 0;
        if (mOptions.testFlag(eoTabsToSpaces)) {
            int cols = charToColumn(mCaretY,mCaretX);
            i = tabWidth() - (cols) % tabWidth();
            Spaces = QString(i,' ');
            NewCaretX = mCaretX + i;
        } else {
            Spaces = '\t';
            NewCaretX = mCaretX + 1;
        }
        setSelTextPrimitive(QStringList(Spaces));
    }
    ensureCursorPosVisible();
}

void SynEdit::doShiftTabKey()
{
    // Provide Visual Studio like block indenting
    if (mOptions.testFlag(eoTabIndent) && canDoBlockIndent()) {
      doBlockUnindent();
      return;
    }

    //Don't un-tab if caret is not on line or is beyond line end
    if (mCaretY > mDocument->count() || mCaretX > lineText().length()+1)
        return;
    //Don't un-tab if no chars before the Caret
    if (mCaretX==1)
        return;
    QString s = lineText().mid(0,mCaretX-1);
    //Only un-tab if caret is at the begin of the line
    if (!s.trimmed().isEmpty())
        return;

    int NewX = 0;
    if (s[s.length()-1] == '\t') {
        NewX= mCaretX-1;
    } else {
        int colsBefore = charToColumn(mCaretY,mCaretX)-1;
        int spacesToRemove = colsBefore % tabWidth();
        if (spacesToRemove == 0)
            spacesToRemove = tabWidth();
        if (spacesToRemove > colsBefore )
            spacesToRemove = colsBefore;
        NewX = mCaretX;
        while (spacesToRemove > 0 && s[NewX-2] == ' ' ) {
            NewX--;
            spacesToRemove--;
        }
    }
    // perform un-tab

    if (NewX != mCaretX) {
        doDeleteText(BufferCoord{NewX, mCaretY},caretXY(),mActiveSelectionMode);
        internalSetCaretX(NewX);
    }
}


bool SynEdit::canDoBlockIndent()
{
    BufferCoord BB;
    BufferCoord BE;

    if (selAvail()) {
//        BB = blockBegin();
//        BE = blockEnd();
        return true;
    } else {
        BB = caretXY();
        BE = caretXY();
    }


    if (BB.Line > mDocument->count() || BE.Line > mDocument->count()) {
        return false;
    }

    if (mActiveSelectionMode == SynSelectionMode::smNormal) {
        QString s = mDocument->getString(BB.Line-1).mid(0,BB.Char-1);
        if (!s.trimmed().isEmpty())
            return false;
        if (BE.Char>1) {
            QString s1=mDocument->getString(BE.Line-1).mid(BE.Char-1);
            QString s2=mDocument->getString(BE.Line-1).mid(0,BE.Char-1);
            if (!s1.trimmed().isEmpty() && !s2.trimmed().isEmpty())
                return false;
        }
    }
    if (mActiveSelectionMode == SynSelectionMode::smColumn) {
        int startCol = charToColumn(BB.Line,BB.Char);
        int endCol = charToColumn(BE.Line,BE.Char);
        for (int i = BB.Line; i<=BE.Line;i++) {
            QString line = mDocument->getString(i-1);
            int startChar = columnToChar(i,startCol);
            QString s = line.mid(0,startChar-1);
            if (!s.trimmed().isEmpty())
                return false;

            int endChar = columnToChar(i,endCol);
            s=line.mid(endChar-1);
            if (!s.trimmed().isEmpty())
                return false;
        }
    }
    return true;
}

QRect SynEdit::calculateCaretRect() const
{
    DisplayCoord coord = displayXY();
    if (!mInputPreeditString.isEmpty()) {
        QString sLine = lineText().left(mCaretX-1)
                + mInputPreeditString
                + lineText().mid(mCaretX-1);
        coord.Column = charToColumn(sLine,mCaretX+mInputPreeditString.length());
    }
    int rows=1;
    if (mActiveSelectionMode == SynSelectionMode::smColumn) {
        int startRow = lineToRow(std::min(blockBegin().Line, blockEnd().Line));
        int endRow = lineToRow(std::max(blockBegin().Line, blockEnd().Line));
        coord.Row = startRow;
        rows = endRow-startRow+1;
    }
    QPoint caretPos = rowColumnToPixels(coord);
    int caretWidth=mCharWidth;
    if (mCaretY <= mDocument->count() && mCaretX <= mDocument->getString(mCaretY-1).length()) {
        caretWidth = charColumns(getDisplayStringAtLine(mCaretY)[mCaretX-1])*mCharWidth;
    }
    if (mActiveSelectionMode == SynSelectionMode::smColumn) {
        return QRect(caretPos.x(),caretPos.y(),caretWidth,
                     mTextHeight*(rows));
    } else {
        return QRect(caretPos.x(),caretPos.y(),caretWidth,
                     mTextHeight);
    }
}

QRect SynEdit::calculateInputCaretRect() const
{
    DisplayCoord coord = displayXY();
    QPoint caretPos = rowColumnToPixels(coord);
    int caretWidth=mCharWidth;
    if (mCaretY <= mDocument->count() && mCaretX <= mDocument->getString(mCaretY-1).length()) {
        caretWidth = charColumns(mDocument->getString(mCaretY-1)[mCaretX-1])*mCharWidth;
    }
    return QRect(caretPos.x(),caretPos.y(),caretWidth,
                 mTextHeight);
}

void SynEdit::clearAreaList(SynEditingAreaList areaList)
{
    areaList.clear();
}

void SynEdit::computeCaret()
{
    QPoint iMousePos = QCursor::pos();
    iMousePos = mapFromGlobal(iMousePos);
    int X=iMousePos.x();
    int Y=iMousePos.y();

    DisplayCoord vCaretNearestPos = pixelsToNearestRowColumn(X, Y);
    vCaretNearestPos.Row = minMax(vCaretNearestPos.Row, 1, displayLineCount());
    setInternalDisplayXY(vCaretNearestPos);
}

void SynEdit::computeScroll(bool isDragging)
{
    QPoint iMousePos = QCursor::pos();
    iMousePos = mapFromGlobal(iMousePos);
    int X=iMousePos.x();
    int Y=iMousePos.y();

    QRect iScrollBounds; // relative to the client area
    int dispX=2,dispY = 2;
//    if (isDragging) {
//        dispX = mCharWidth / 2 -1;
//        dispY = mTextHeight/ 2 -1;
//    }
    int left = mGutterWidth+frameWidth()+dispX;
    int top = frameWidth()+dispY;
    iScrollBounds = QRect(left,
                          top,
                          clientWidth()-left-dispX,
                          clientHeight()-top-dispY);

    if (X < iScrollBounds.left())
        mScrollDeltaX = (X - iScrollBounds.left()) / mCharWidth - 1;
    else if (X >= iScrollBounds.right())
        mScrollDeltaX = (X - iScrollBounds.right()) / mCharWidth + 1;
    else
        mScrollDeltaX = 0;

//    if (isDragging && (X<0 || X>clientRect().width())) {
//        mScrollDeltaX = 0;
//    }

    if (Y < iScrollBounds.top())
        mScrollDeltaY = (Y - iScrollBounds.top()) / mTextHeight - 1;
    else if (Y >= iScrollBounds.bottom())
        mScrollDeltaY = (Y - iScrollBounds.bottom()) / mTextHeight + 1;
    else
        mScrollDeltaY = 0;

//    if (isDragging && (Y<0 || Y>clientRect().height())) {
//        mScrollDeltaY = 0;
//    }


//    if (mScrollDeltaX!=0 || mScrollDeltaY!=0) {
    if (isDragging) {
        mScrollTimer->singleShot(100,this,&SynEdit::onDraggingScrollTimeout);
    } else  {
        mScrollTimer->singleShot(100,this,&SynEdit::onScrollTimeout);
    }
//    }
}

void SynEdit::doBlockIndent()
{
    BufferCoord  oldCaretPos;
    BufferCoord  BB, BE;
    QStringList strToInsert;
    int e,x,i;
    QString spaces;
    BufferCoord insertionPos;

    oldCaretPos = caretXY();

    // keep current selection detail
    if (selAvail()) {
        BB = blockBegin();
        BE = blockEnd();
    } else {
        BB = caretXY();
        BE = caretXY();
    }
    // build text to insert
    if (BE.Char == 1 && BE.Line != BB.Line) {
        e = BE.Line - 1;
        x = 1;
    } else {
        e = BE.Line;
        if (mOptions.testFlag(SynEditorOption::eoTabsToSpaces))
          x = caretX() + tabWidth();
        else
          x = caretX() + 1;
    }
    if (mOptions.testFlag(eoTabsToSpaces)) {
        spaces = QString(tabWidth(),' ') ;
    } else {
        spaces = "\t";
    }
    for (i = BB.Line; i<e;i++) {
        strToInsert.append(spaces);
    }
    strToInsert.append(spaces);
    mUndoList->BeginBlock();
    mUndoList->AddChange(SynChangeReason::crCaret, oldCaretPos, oldCaretPos,QStringList(), activeSelectionMode());
    mUndoList->AddChange(SynChangeReason::crSelection,mBlockBegin,mBlockEnd,QStringList(), activeSelectionMode());
    insertionPos.Line = BB.Line;
    if (mActiveSelectionMode == SynSelectionMode::smColumn)
      insertionPos.Char = std::min(BB.Char, BE.Char);
    else
      insertionPos.Char = 1;
    insertBlock(insertionPos, insertionPos, strToInsert);
    //adjust caret and selection
    oldCaretPos.Char = x;
    if (BB.Char > 1)
        BB.Char += spaces.length();
    if (BE.Char > 1)
      BE.Char+=spaces.length();
    setCaretAndSelection(oldCaretPos,
      BB, BE);
    mUndoList->EndBlock();
}

void SynEdit::doBlockUnindent()
{
    int lastIndent = 0;
    int firstIndent = 0;

    BufferCoord BB,BE;
    // keep current selection detail
    if (selAvail()) {
        BB = blockBegin();
        BE = blockEnd();
    } else {
        BB = caretXY();
        BE = caretXY();
    }
    BufferCoord oldCaretPos = caretXY();
    int x = 0;
    mUndoList->BeginBlock();
    mUndoList->AddChange(SynChangeReason::crCaret, oldCaretPos, oldCaretPos,QStringList(), activeSelectionMode());
    mUndoList->AddChange(SynChangeReason::crSelection,mBlockBegin,mBlockEnd,QStringList(), activeSelectionMode());

    int e = BE.Line;
    // convert selection to complete lines
    if (BE.Char == 1)
        e = BE.Line - 1;
    // build string to delete
    for (int i = BB.Line; i<= e;i++) {
        QString line = mDocument->getString(i - 1);
        if (line.isEmpty())
            continue;
        if (line[0]!=' ' && line[0]!='\t')
            continue;
        int charsToDelete = 0;
        while (charsToDelete < tabWidth() &&
               charsToDelete < line.length() &&
               line[charsToDelete] == ' ')
            charsToDelete++;
        if (charsToDelete == 0)
            charsToDelete = 1;
        if (i==BB.Line)
            firstIndent = charsToDelete;
        if (i==e)
            lastIndent = charsToDelete;
        if (i==oldCaretPos.Line)
            x = charsToDelete;
        QString tempString = line.mid(charsToDelete);
        mDocument->putString(i-1,tempString);
        mUndoList->AddChange(SynChangeReason::crDelete,
                             BufferCoord{1,i},
                             BufferCoord{charsToDelete+1,i},
                             QStringList(line.left(charsToDelete)),
                             SynSelectionMode::smNormal);
    }
  // restore selection
  //adjust the x position of orgcaretpos appropriately

    oldCaretPos.Char -= x;
    BB.Char -= firstIndent;
    BE.Char -= lastIndent;
    setCaretAndSelection(oldCaretPos, BB, BE);
    mUndoList->EndBlock();
}

void SynEdit::doAddChar(QChar AChar)
{
    if (mReadOnly)
        return;
    if (!AChar.isPrint() && AChar!='\t')
        return;
    //DoOnPaintTransient(ttBefore);
    //mCaretX will change after setSelLength;
    if (mInserting == false && !selAvail()) {
        switch(mActiveSelectionMode) {
        case SynSelectionMode::smColumn: {
            //we can't use blockBegin()/blockEnd()
            BufferCoord start=mBlockBegin;
            BufferCoord end=mBlockEnd;
            if (start.Line > end.Line )
                std::swap(start,end);
            start.Char++; // make sure we select a whole char in the start line
            setBlockBegin(start);
            setBlockEnd(end);
        }
            break;
        case SynSelectionMode::smLine:
            //do nothing;
            break;
        default:
            setSelLength(1);
        }
    }

    if (isIdentChar(AChar)) {
        doSetSelText(AChar);
    } else if (AChar.isSpace()) {
        // break group undo chain
        mUndoList->AddGroupBreak();
        doSetSelText(AChar);
        // break group undo chain
//        if (mActiveSelectionMode!=SynSelectionMode::smColumn)
//            mUndoList->AddChange(SynChangeReason::crNothing,
//                                 BufferCoord{0, 0},
//                                 BufferCoord{0, 0},
//                                 "", SynSelectionMode::smNormal);
    } else {
        mUndoList->BeginBlock();
        doSetSelText(AChar);
        int oldCaretX=mCaretX-1;
        int oldCaretY=mCaretY;
        // auto
        if (mActiveSelectionMode==SynSelectionMode::smNormal
                && mOptions.testFlag(eoAutoIndent)
                && mHighlighter
                && mHighlighter->getClass()==SynHighlighterClass::CppHighlighter
                && (oldCaretY<=mDocument->count()) ) {

            //unindent if ':' at end of the line
            if (AChar == ':') {
                QString line = mDocument->getString(oldCaretY-1);
                if (line.length() <= oldCaretX) {
                    int indentSpaces = calcIndentSpaces(oldCaretY,line+":", true);
                    if (indentSpaces != leftSpaces(line)) {
                        QString newLine = GetLeftSpacing(indentSpaces,true) + trimLeft(line);
                        mDocument->putString(oldCaretY-1,newLine);
                        internalSetCaretXY(BufferCoord{newLine.length()+2,oldCaretY});
                        setBlockBegin(caretXY());
                        setBlockEnd(caretXY());
                        mUndoList->AddChange(
                                    SynChangeReason::crDelete,
                                    BufferCoord{1, oldCaretY},
                                    BufferCoord{line.length()+1, oldCaretY},
                                    QStringList(line),
                                    SynSelectionMode::smNormal
                                    );
                        mUndoList->AddChange(
                                    SynChangeReason::crInsert,
                                    BufferCoord{1, oldCaretY},
                                    BufferCoord{newLine.length()+1, oldCaretY},
                                    QStringList(),
                                    SynSelectionMode::smNormal
                                    );
                    }
                }
            } else if (AChar == '*') {
                QString line = mDocument->getString(oldCaretY-1);
                if (line.length() <= oldCaretX) {
                    int indentSpaces = calcIndentSpaces(oldCaretY,line+"*", true);
                    if (indentSpaces != leftSpaces(line)) {
                        QString newLine = GetLeftSpacing(indentSpaces,true) + trimLeft(line);
                        mDocument->putString(oldCaretY-1,newLine);
                        internalSetCaretXY(BufferCoord{newLine.length()+2,oldCaretY});
                        setBlockBegin(caretXY());
                        setBlockEnd(caretXY());
                        mUndoList->AddChange(
                                    SynChangeReason::crDelete,
                                    BufferCoord{1, oldCaretY},
                                    BufferCoord{line.length()+1, oldCaretY},
                                    QStringList(line),
                                    SynSelectionMode::smNormal
                                    );
                        mUndoList->AddChange(
                                    SynChangeReason::crInsert,
                                    BufferCoord{1, oldCaretY},
                                    BufferCoord{newLine.length()+1, oldCaretY},
                                    QStringList(),
                                    SynSelectionMode::smNormal
                                    );
                    }
                }
            } else if (AChar == '{' || AChar == '}' || AChar == '#') {
                //Reindent line when add '{' '}' and '#' at the beginning
                QString left = mDocument->getString(oldCaretY-1).mid(0,oldCaretX-1);
                // and the first nonblank char is this new {
                if (left.trimmed().isEmpty()) {
                    int indentSpaces = calcIndentSpaces(oldCaretY,AChar, true);
                    if (indentSpaces != leftSpaces(left)) {
                        QString right = mDocument->getString(oldCaretY-1).mid(oldCaretX-1);
                        QString newLeft = GetLeftSpacing(indentSpaces,true);
                        mDocument->putString(oldCaretY-1,newLeft+right);
                        BufferCoord newCaretPos =  BufferCoord{newLeft.length()+2,oldCaretY};
                        internalSetCaretXY(newCaretPos);
                        setBlockBegin(caretXY());
                        setBlockEnd(caretXY());
                        mUndoList->AddChange(
                                    SynChangeReason::crDelete,
                                    BufferCoord{1, oldCaretY},
                                    BufferCoord{left.length()+1, oldCaretY},
                                    QStringList(left),
                                    SynSelectionMode::smNormal
                                    );
                        mUndoList->AddChange(
                                    SynChangeReason::crInsert,
                                    BufferCoord{1, oldCaretY},
                                    BufferCoord{newLeft.length()+1, oldCaretY},
                                    QStringList(""),
                                    SynSelectionMode::smNormal
                                    );

                    }
                }
            }
        }
        mUndoList->EndBlock();
    }
    //DoOnPaintTransient(ttAfter);
}

void SynEdit::doCutToClipboard()
{
    if (mReadOnly)
        return;
    mUndoList->BeginBlock();
    mUndoList->AddChange(
                SynChangeReason::crCaret,
                caretXY(),
                caretXY(),
                QStringList(),
                activeSelectionMode());
    mUndoList->AddChange(
                SynChangeReason::crSelection,
                mBlockBegin,
                mBlockEnd,
                QStringList(),
                SynSelectionMode::smNormal);
    if (!selAvail()) {
        doSelecteLine();
    }
    internalDoCopyToClipboard(selText());
    setSelectedTextEmpty();
    mUndoList->EndBlock();
    mUndoList->AddGroupBreak();
}

void SynEdit::doCopyToClipboard()
{
    bool selected=selAvail();
    if (!selected)
        doSelecteLine();
    bool ChangeTrim = (mActiveSelectionMode == SynSelectionMode::smColumn) &&
            mOptions.testFlag(eoTrimTrailingSpaces);
    QString sText;
    {
        auto action = finally([&,this] {
            if (ChangeTrim)
                mOptions.setFlag(eoTrimTrailingSpaces);
        });
        if (ChangeTrim)
            mOptions.setFlag(eoTrimTrailingSpaces,false);
        sText = selText();
    }
    internalDoCopyToClipboard(sText);
    if (!selected) {
        setBlockBegin(caretXY());
        setBlockEnd(caretXY());
    }
}

void SynEdit::internalDoCopyToClipboard(const QString &s)
{
    QClipboard* clipboard=QGuiApplication::clipboard();
    clipboard->clear();
    clipboard->setText(s);
}

void SynEdit::doPasteFromClipboard()
{
    if (mReadOnly)
        return;
    QClipboard* clipboard = QGuiApplication::clipboard();
    QString text = clipboard->text();
    if (text.isEmpty())
        return;
    doOnPaintTransient(SynTransientType::ttBefore);
    mUndoList->BeginBlock();
//    if (selAvail()) {
//        mUndoList->AddChange(
//                    SynChangeReason::crDelete,
//                    mBlockBegin,
//                    mBlockEnd,
//                    selText(),
//                    mActiveSelectionMode);
//    }
//        } else if (!colSelAvail())
//            setActiveSelectionMode(selectionMode());
    BufferCoord vStartOfBlock = blockBegin();
    BufferCoord vEndOfBlock = blockEnd();
    mBlockBegin = vStartOfBlock;
    mBlockEnd = vEndOfBlock;
    qDebug()<<textToLines(text);
    setSelTextPrimitive(splitStrings(text));
    mUndoList->EndBlock();
}

void SynEdit::incPaintLock()
{
    if (mPaintLock==0) {
        onBeginFirstPaintLock();
    }
    mPaintLock ++ ;
}

void SynEdit::decPaintLock()
{
    Q_ASSERT(mPaintLock > 0);
    mPaintLock--;
    if (mPaintLock == 0 ) {
        if (mStateFlags.testFlag(SynStateFlag::sfScrollbarChanged)) {
            updateScrollbars();
            ensureCursorPosVisible();
        }
        if (mStateFlags.testFlag(SynStateFlag::sfCaretChanged))
            updateCaret();
        if (mStatusChanges!=0)
            doOnStatusChange(mStatusChanges);
        onEndFirstPaintLock();
    }
}

int SynEdit::clientWidth()
{
    return viewport()->size().width();
}

int SynEdit::clientHeight()
{
    return viewport()->size().height();
}

int SynEdit::clientTop()
{
    return 0;
}

int SynEdit::clientLeft()
{
    return 0;
}

QRect SynEdit::clientRect()
{
    return QRect(0,0, clientWidth(), clientHeight());
}

void SynEdit::synFontChanged()
{
    recalcCharExtent();
    onSizeOrFontChanged(true);
}

void SynEdit::doOnPaintTransient(SynTransientType TransientType)
{
    doOnPaintTransientEx(TransientType, false);
}

void SynEdit::updateLastCaretX()
{
    mLastCaretColumn = displayX();
}

void SynEdit::ensureCursorPosVisible()
{
    ensureCursorPosVisibleEx(false);
}

void SynEdit::ensureCursorPosVisibleEx(bool ForceToMiddle)
{
    incPaintLock();
    auto action = finally([this]{
        decPaintLock();
    });
    // Make sure X is visible
    int VisibleX = displayX();
    if (VisibleX < leftChar())
        setLeftChar(VisibleX);
    else if (VisibleX >= mCharsInWindow + leftChar() && mCharsInWindow > 0)
        setLeftChar(VisibleX - mCharsInWindow + 1);
    else
        setLeftChar(leftChar());
    // Make sure Y is visible
    int vCaretRow = displayY();
    if (ForceToMiddle) {
        if (vCaretRow < mTopLine || vCaretRow>(mTopLine + (mLinesInWindow - 1)))
            setTopLine( vCaretRow - (mLinesInWindow - 1) / 2);
    } else {
        if (vCaretRow < mTopLine)
          setTopLine(vCaretRow);
        else if (vCaretRow > mTopLine + (mLinesInWindow - 1) && mLinesInWindow > 0)
          setTopLine(vCaretRow - (mLinesInWindow - 1));
        else
          setTopLine(mTopLine);
    }
}

void SynEdit::scrollWindow(int dx, int dy)
{
    int nx = horizontalScrollBar()->value()+dx;
    int ny = verticalScrollBar()->value()+dy;
    horizontalScrollBar()->setValue(nx);
    verticalScrollBar()->setValue(ny);
}

void SynEdit::setInternalDisplayXY(const DisplayCoord &aPos)
{
    incPaintLock();
    internalSetCaretXY(displayToBufferPos(aPos));
    decPaintLock();
    updateLastCaretX();
}

void SynEdit::internalSetCaretXY(const BufferCoord &Value)
{
    setCaretXYEx(true, Value);
}

void SynEdit::internalSetCaretX(int Value)
{
    internalSetCaretXY(BufferCoord{Value, mCaretY});
}

void SynEdit::internalSetCaretY(int Value)
{
    internalSetCaretXY(BufferCoord{mCaretX,Value});
}

void SynEdit::setStatusChanged(SynStatusChanges changes)
{
    mStatusChanges = mStatusChanges | changes;
    if (mPaintLock == 0)
        doOnStatusChange(mStatusChanges);
}

void SynEdit::doOnStatusChange(SynStatusChanges)
{
    if (mStatusChanges.testFlag(SynStatusChange::scCaretX)
            || mStatusChanges.testFlag(SynStatusChange::scCaretY)) {
        qApp->inputMethod()->update(Qt::ImCursorPosition);
    }
    emit statusChanged(mStatusChanges);
    mStatusChanges = SynStatusChange::scNone;
}

void SynEdit::insertBlock(const BufferCoord& startPos, const BufferCoord& endPos, const QStringList& blockText)
{
    setCaretAndSelection(startPos, startPos, startPos);
    setSelTextPrimitiveEx(SynSelectionMode::smColumn, blockText);
}

void SynEdit::updateScrollbars()
{
    int nMaxScroll;
    int nMin,nMax,nPage,nPos;
    if (mPaintLock!=0) {
        mStateFlags.setFlag(SynStateFlag::sfScrollbarChanged);
    } else {
        mStateFlags.setFlag(SynStateFlag::sfScrollbarChanged,false);
        if (mScrollBars != SynScrollStyle::ssNone) {
            if (mOptions.testFlag(eoHideShowScrollbars)) {
                setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
                setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAsNeeded);
            } else {
                setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOn);
                setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOn);
            }
            if (mScrollBars == SynScrollStyle::ssBoth ||  mScrollBars == SynScrollStyle::ssHorizontal) {
                nMaxScroll = maxScrollWidth();
                if (nMaxScroll <= MAX_SCROLL) {
                    nMin = 1;
                    nMax = nMaxScroll;
                    nPage = mCharsInWindow;
                    nPos = mLeftChar;
                } else {
                    nMin = 0;
                    nMax = MAX_SCROLL;
                    nPage = mulDiv(MAX_SCROLL, mCharsInWindow, nMaxScroll);
                    nPos = mulDiv(MAX_SCROLL, mLeftChar, nMaxScroll);
                }
                horizontalScrollBar()->setMinimum(nMin);
                horizontalScrollBar()->setMaximum(nMax);
                horizontalScrollBar()->setPageStep(nPage);
                horizontalScrollBar()->setValue(nPos);
                horizontalScrollBar()->setSingleStep(1);
            } else
                setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOn);

            if (mScrollBars == SynScrollStyle::ssBoth ||  mScrollBars == SynScrollStyle::ssVertical) {
                nMaxScroll = maxScrollHeight();
                if (nMaxScroll <= MAX_SCROLL) {
                    nMin = 1;
                    nMax = std::max(1, nMaxScroll);
                    nPage = mLinesInWindow;
                    nPos = mTopLine;
                } else {
                    nMin = 0;
                    nMax = MAX_SCROLL;
                    nPage = mulDiv(MAX_SCROLL, mLinesInWindow, nMaxScroll);
                    nPos = mulDiv(MAX_SCROLL, mTopLine, nMaxScroll);
                }
                verticalScrollBar()->setMinimum(nMin);
                verticalScrollBar()->setMaximum(nMax);
                verticalScrollBar()->setPageStep(nPage);
                verticalScrollBar()->setValue(nPos);
                verticalScrollBar()->setSingleStep(1);
            } else
                setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        } else {
            setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
            setVerticalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        }
    }
}

void SynEdit::updateCaret()
{
    mStateFlags.setFlag(SynStateFlag::sfCaretChanged,false);
    invalidateRect(calculateCaretRect());
}

void SynEdit::recalcCharExtent()
{
    SynFontStyle styles[] = {SynFontStyle::fsBold, SynFontStyle::fsItalic, SynFontStyle::fsStrikeOut, SynFontStyle::fsUnderline};
    bool hasStyles[] = {false,false,false,false};
    int size = 4;
    if (mHighlighter && mHighlighter->attributes().count()>0) {
        for (const PSynHighlighterAttribute& attribute: mHighlighter->attributes()) {
            for (int i=0;i<size;i++) {
                if (attribute->styles().testFlag(styles[i]))
                    hasStyles[i] = true;
            }
        }
    } else {
        hasStyles[0] = font().bold();
        hasStyles[1] = font().italic();
        hasStyles[2] = font().strikeOut();
        hasStyles[3] = font().underline();
    }

    mTextHeight  = 0;
    mCharWidth = 0;
    QFontMetrics fm(font());
    QFontMetrics fm2(font());
    mTextHeight = std::max(fm.lineSpacing(),fm2.lineSpacing());
    mCharWidth = fm.horizontalAdvance("M");

    if (hasStyles[0]) { // has bold font
        QFont f = font();
        f.setBold(true);
        QFontMetrics fm(f);
        QFont f2 = font();
        f2.setBold(true);
        QFontMetrics fm2(f);
        if (fm.lineSpacing()>mTextHeight)
            mTextHeight=fm.lineSpacing();
        if (fm2.lineSpacing()>mTextHeight)
            mTextHeight=fm2.lineSpacing();
        if (fm.horizontalAdvance("M")>mCharWidth)
            mCharWidth = fm.horizontalAdvance("M");
    }
    if (hasStyles[1]) { // has strike out font
        QFont f = font();
        f.setItalic(true);
        QFontMetrics fm(f);
        QFont f2 = font();
        f2.setItalic(true);
        QFontMetrics fm2(f);
        if (fm.lineSpacing()>mTextHeight)
            mTextHeight=fm.lineSpacing();
        if (fm2.lineSpacing()>mTextHeight)
            mTextHeight=fm2.lineSpacing();
        if (fm.horizontalAdvance("M")>mCharWidth)
            mCharWidth = fm.horizontalAdvance("M");
    }
    if (hasStyles[2]) { // has strikeout
        QFont f = font();
        f.setStrikeOut(true);
        QFontMetrics fm(f);
        QFont f2 = font();
        f2.setStrikeOut(true);
        QFontMetrics fm2(f);
        if (fm.lineSpacing()>mTextHeight)
            mTextHeight=fm.lineSpacing();
        if (fm2.lineSpacing()>mTextHeight)
            mTextHeight=fm2.lineSpacing();
        if (fm.horizontalAdvance("M")>mCharWidth)
            mCharWidth = fm.horizontalAdvance("M");
    }
    if (hasStyles[3]) { // has underline
        QFont f = font();
        f.setUnderline(true);
        QFontMetrics fm(f);
        QFont f2 = font();
        f2.setUnderline(true);
        QFontMetrics fm2(f);
        if (fm.lineSpacing()>mTextHeight)
            mTextHeight=fm.lineSpacing();
        if (fm2.lineSpacing()>mTextHeight)
            mTextHeight=fm2.lineSpacing();
        if (fm.horizontalAdvance("M")>mCharWidth)
            mCharWidth = fm.horizontalAdvance("M");
    }
    mTextHeight += mExtraLineSpacing;
}

QString SynEdit::expandAtWideGlyphs(const QString &S)
{
    QString Result(S.length()*2); // speed improvement
    int  j = 0;
    for (int i=0;i<S.length();i++) {
        int CountOfAvgGlyphs = ceil(fontMetrics().horizontalAdvance(S[i])/(double)mCharWidth);
        if (j+CountOfAvgGlyphs>=Result.length())
            Result.resize(Result.length()+128);
        // insert CountOfAvgGlyphs filling chars
        while (CountOfAvgGlyphs>1) {
            Result[j]=QChar(0xE000);
            j++;
            CountOfAvgGlyphs--;
        }
        Result[j]=S[i];
        j++;
    }
    Result.resize(j);
    return Result;
}

void SynEdit::updateModifiedStatus()
{
    setModified(!mUndoList->initialState());
}

int SynEdit::scanFrom(int Index, int canStopIndex)
{
    SynRangeState iRange;
    int Result = std::max(0,Index);
    if (Result >= mDocument->count())
        return Result;

    if (Result == 0) {
        mHighlighter->resetState();
    } else {
        mHighlighter->setState(mDocument->ranges(Result-1));
    }
    do {
        mHighlighter->setLine(mDocument->getString(Result), Result);
        mHighlighter->nextToEol();
        iRange = mHighlighter->getRangeState();
        if (Result > canStopIndex){
            if (mDocument->ranges(Result).state == iRange.state
                    && mDocument->ranges(Result).braceLevel == iRange.braceLevel
                    && mDocument->ranges(Result).parenthesisLevel == iRange.parenthesisLevel
                    && mDocument->ranges(Result).bracketLevel == iRange.bracketLevel
                    ) {
                if (mUseCodeFolding)
                    rescanFolds();
                return Result;// avoid the final Decrement
            }
        }
        mDocument->setRange(Result,iRange);
        Result ++ ;
    } while (Result < mDocument->count());
    Result--;
    if (mUseCodeFolding)
        rescanFolds();
    return Result;
}

void SynEdit::rescanRange(int line)
{
    if (!mHighlighter)
        return;
    line--;
    line = std::max(0,line);
    if (line >= mDocument->count())
        return;

    if (line == 0) {
        mHighlighter->resetState();
    } else {
        mHighlighter->setState(mDocument->ranges(line-1));
    }
    mHighlighter->setLine(mDocument->getString(line), line);
    mHighlighter->nextToEol();
    SynRangeState iRange = mHighlighter->getRangeState();
    mDocument->setRange(line,iRange);
}

void SynEdit::rescanRanges()
{
    if (mHighlighter && !mDocument->empty()) {
        mHighlighter->resetState();
        for (int i =0;i<mDocument->count();i++) {
            mHighlighter->setLine(mDocument->getString(i), i);
            mHighlighter->nextToEol();
            mDocument->setRange(i, mHighlighter->getRangeState());
        }
    }
    if (mUseCodeFolding)
        rescanFolds();
}

void SynEdit::uncollapse(PSynEditFoldRange FoldRange)
{
    FoldRange->linesCollapsed = 0;
    FoldRange->collapsed = false;

    // Redraw the collapsed line
    invalidateLines(FoldRange->fromLine, INT_MAX);

    // Redraw fold mark
    invalidateGutterLines(FoldRange->fromLine, INT_MAX);
    updateScrollbars();
}

void SynEdit::collapse(PSynEditFoldRange FoldRange)
{
    FoldRange->linesCollapsed = FoldRange->toLine - FoldRange->fromLine;
    FoldRange->collapsed = true;

    // Extract caret from fold
    if ((mCaretY > FoldRange->fromLine) && (mCaretY <= FoldRange->toLine)) {
          setCaretXY(BufferCoord{mDocument->getString(FoldRange->fromLine - 1).length() + 1,
                                 FoldRange->fromLine});
    }

    // Redraw the collapsed line
    invalidateLines(FoldRange->fromLine, INT_MAX);

    // Redraw fold mark
    invalidateGutterLines(FoldRange->fromLine, INT_MAX);

    updateScrollbars();
}

void SynEdit::foldOnListInserted(int Line, int Count)
{
    // Delete collapsed inside selection
    for (int i = mAllFoldRanges.count()-1;i>=0;i--) {
        PSynEditFoldRange range = mAllFoldRanges[i];
        if (range->collapsed || range->parentCollapsed()){
            if (range->fromLine == Line - 1) // insertion starts at fold line
                uncollapse(range);
            else if (range->fromLine >= Line) // insertion of count lines above FromLine
                range->move(Count);
        }
    }
}

void SynEdit::foldOnListDeleted(int Line, int Count)
{
    // Delete collapsed inside selection
    for (int i = mAllFoldRanges.count()-1;i>=0;i--) {
        PSynEditFoldRange range = mAllFoldRanges[i];
        if (range->collapsed || range->parentCollapsed()){
            if (range->fromLine == Line && Count == 1)  // open up because we are messing with the starting line
                uncollapse(range);
            else if (range->fromLine >= Line - 1 && range->fromLine < Line + Count) // delete inside affectec area
                mAllFoldRanges.remove(i);
            else if (range->fromLine >= Line + Count) // Move after affected area
                range->move(-Count);
        }
    }

}

void SynEdit::foldOnListCleared()
{
    mAllFoldRanges.clear();
}

void SynEdit::rescanFolds()
{
    if (!mUseCodeFolding)
        return;
    rescanForFoldRanges();
    invalidateGutter();
}

static void null_deleter(SynEditFoldRanges *) {}

void SynEdit::rescanForFoldRanges()
{
    // Delete all uncollapsed folds
    for (int i=mAllFoldRanges.count()-1;i>=0;i--) {
        PSynEditFoldRange range =mAllFoldRanges[i];
        if (!range->collapsed && !range->parentCollapsed())
            mAllFoldRanges.remove(i);
    }

    // Did we leave any collapsed folds and are we viewing a code file?
    if (mAllFoldRanges.count() > 0) {

        // Add folds to a separate list
        PSynEditFoldRanges TemporaryAllFoldRanges = std::make_shared<SynEditFoldRanges>();
        scanForFoldRanges(TemporaryAllFoldRanges);
        SynEditFoldRanges ranges=mAllFoldRanges;
        mAllFoldRanges.clear();

        // Combine new with old folds, preserve parent order
        for (int i = 0; i< TemporaryAllFoldRanges->count();i++) {
            int j=0;
            while (j <ranges.count()) {
                if (TemporaryAllFoldRanges->range(i)->fromLine == ranges[j]->fromLine
                        && TemporaryAllFoldRanges->range(i)->toLine == ranges[j]->toLine
                        && ranges[j]->collapsed) {
                    mAllFoldRanges.add(ranges[j]);
                    break;
                }
                j++;
            }
            if (j>=ranges.count())
                mAllFoldRanges.add(TemporaryAllFoldRanges->range(i));
        }

    } else {

        // We ended up with no folds after deleting, just pass standard data...
        PSynEditFoldRanges temp(&mAllFoldRanges, null_deleter);
        scanForFoldRanges(temp);
    }
}

void SynEdit::scanForFoldRanges(PSynEditFoldRanges TopFoldRanges)
{
    PSynEditFoldRanges parentFoldRanges = TopFoldRanges;
    // Recursively scan for folds (all types)
    for (int i= 0 ; i< mCodeFolding.foldRegions.count() ; i++ ) {
        findSubFoldRange(TopFoldRanges, i,parentFoldRanges,PSynEditFoldRange());
    }
}

//this func should only be used in findSubFoldRange
int SynEdit::lineHasChar(int Line, int startChar, QChar character, const QString& highlighterAttrName) {
    QString CurLine = mDocument->getString(Line);
    if (!mHighlighter){
        for (int i=startChar; i<CurLine.length();i++) {
            if (CurLine[i]==character) {
                return i;
            }
        }

    } else {
        /*
        mHighlighter->setState(mLines->ranges(Line),
                               mLines->braceLevels(Line),
                               mLines->bracketLevels(Line),
                               mLines->parenthesisLevels(Line));
        mHighlighter->setLine(CurLine,Line);
        */
        QString token;
        while (!mHighlighter->eol()) {
            token = mHighlighter->getToken();
            PSynHighlighterAttribute attr = mHighlighter->getTokenAttribute();
            if (token == character && attr->name()==highlighterAttrName)
                return mHighlighter->getTokenPos();
            mHighlighter->next();
        }
    }
    return -1;
}

void SynEdit::findSubFoldRange(PSynEditFoldRanges TopFoldRanges, int FoldIndex,PSynEditFoldRanges& parentFoldRanges, PSynEditFoldRange Parent)
{
    PSynEditFoldRange  CollapsedFold;
    int Line = 0;
    QString CurLine;
    if (!mHighlighter)
        return;
    bool useBraces = ( mCodeFolding.foldRegions.get(FoldIndex)->openSymbol == "{"
            && mCodeFolding.foldRegions.get(FoldIndex)->closeSymbol == "}");

    while (Line < mDocument->count()) { // index is valid for LinesToScan and fLines
        // If there is a collapsed fold over here, skip it
        CollapsedFold = collapsedFoldStartAtLine(Line + 1); // only collapsed folds remain
        if (CollapsedFold) {
          Line = CollapsedFold->toLine;
          continue;
        }

        //we just use braceLevel
        if (useBraces) {
            // Find an opening character on this line
            CurLine = mDocument->getString(Line);
            if (mDocument->rightBraces(Line)>0) {
                for (int i=0; i<mDocument->rightBraces(Line);i++) {
                    // Stop the recursion if we find a closing char, and return to our parent
                    if (Parent) {
                      Parent->toLine = Line + 1;
                      Parent = Parent->parent;
                      if (!Parent) {
                          parentFoldRanges = TopFoldRanges;
                      } else {
                          parentFoldRanges = Parent->subFoldRanges;
                      }
                    }
                }
            }
            if (mDocument->leftBraces(Line)>0) {
                for (int i=0; i<mDocument->leftBraces(Line);i++) {
                    // Add it to the top list of folds
                    Parent = parentFoldRanges->addByParts(
                      Parent,
                      TopFoldRanges,
                      Line + 1,
                      mCodeFolding.foldRegions.get(FoldIndex),
                      Line + 1);
                    parentFoldRanges = Parent->subFoldRanges;
                }
            }
        } else {

            // Find an opening character on this line
            CurLine = mDocument->getString(Line);

            mHighlighter->setState(mDocument->ranges(Line));
            mHighlighter->setLine(CurLine,Line);

            QString token;
            int pos;
            while (!mHighlighter->eol()) {
                token = mHighlighter->getToken();
                pos = mHighlighter->getTokenPos()+token.length();
                PSynHighlighterAttribute attr = mHighlighter->getTokenAttribute();
                // We've found a starting character and it have proper highlighting (ignore stuff inside comments...)
                if (token == mCodeFolding.foldRegions.get(FoldIndex)->openSymbol && attr->name()==mCodeFolding.foldRegions.get(FoldIndex)->highlight) {
                    // And ignore lines with both opening and closing chars in them
                    if (lineHasChar(Line,pos,mCodeFolding.foldRegions.get(FoldIndex)->closeSymbol,
                                    mCodeFolding.foldRegions.get(FoldIndex)->highlight)<0) {
                        // Add it to the top list of folds
                        Parent = parentFoldRanges->addByParts(
                          Parent,
                          TopFoldRanges,
                          Line + 1,
                          mCodeFolding.foldRegions.get(FoldIndex),
                          Line + 1);
                        parentFoldRanges = Parent->subFoldRanges;

                        // Skip until a newline
                        break;
                    }

                } else if (token == mCodeFolding.foldRegions.get(FoldIndex)->closeSymbol && attr->name()==mCodeFolding.foldRegions.get(FoldIndex)->highlight) {
                    // And ignore lines with both opening and closing chars in them
                    if (lineHasChar(Line,pos,mCodeFolding.foldRegions.get(FoldIndex)->openSymbol,
                                    mCodeFolding.foldRegions.get(FoldIndex)->highlight)<0) {
                        // Stop the recursion if we find a closing char, and return to our parent
                        if (Parent) {
                          Parent->toLine = Line + 1;
                          Parent = Parent->parent;
                          if (!Parent) {
                              parentFoldRanges = TopFoldRanges;
                          } else {
                              parentFoldRanges = Parent->subFoldRanges;
                          }
                        }

                        // Skip until a newline
                        break;
                    }
                }
                mHighlighter->next();
            }
        }
        Line++;
    }
}

PSynEditFoldRange SynEdit::collapsedFoldStartAtLine(int Line)
{
    for (int i = 0; i< mAllFoldRanges.count() - 1; i++ ) {
        if (mAllFoldRanges[i]->fromLine == Line && mAllFoldRanges[i]->collapsed) {
            return mAllFoldRanges[i];
        } else if (mAllFoldRanges[i]->fromLine > Line) {
            break; // sorted by line. don't bother scanning further
        }
    }
    return PSynEditFoldRange();
}

void SynEdit::doOnPaintTransientEx(SynTransientType , bool )
{
    //todo: we can't draw to canvas outside paintEvent
}

void SynEdit::initializeCaret()
{
    //showCaret();
}

PSynEditFoldRange SynEdit::foldStartAtLine(int Line) const
{
    for (int i = 0; i<mAllFoldRanges.count();i++) {
        PSynEditFoldRange range = mAllFoldRanges[i];
        if (range->fromLine == Line ){
            return range;
        } else if (range->fromLine>Line)
            break; // sorted by line. don't bother scanning further
    }
    return PSynEditFoldRange();
}

bool SynEdit::foldCollapsedBetween(int startLine, int endLine) const
{
    for (int i = 0; i<mAllFoldRanges.count();i++) {
        PSynEditFoldRange range = mAllFoldRanges[i];
        if (startLine >=range->fromLine && range->fromLine<=endLine
                && (range->collapsed || range->parentCollapsed())){
            return true;
        } else if (range->fromLine>endLine)
            break; // sorted by line. don't bother scanning further
    }
    return false;
}

QString SynEdit::substringByColumns(const QString &s, int startColumn, int &colLen)
{

    int len = s.length();
    int columns = 0;
    int i = 0;
    int oldColumns=0;
    while (columns < startColumn) {
        oldColumns = columns;
        if (i>=len)
            break;
        if (s[i] == '\t')
            columns += tabWidth() - (columns % tabWidth());
        else
            columns += charColumns(s[i]);
        i++;
    }
    QString result;
    if (i>=len) {
        colLen = 0;
        return result;
    }
    if (colLen>result.capacity()) {
        result.resize(colLen);
    }
    int j=0;
    if (i>0) {
        result[0]=s[i-1];
        j++;
    }
    while (i<len && columns<startColumn+colLen) {
        result[j]=s[i];
        if (i < len && s[i] == '\t')
            columns += tabWidth() - (columns % tabWidth());
        else
            columns += charColumns(s[i]);
        i++;
        j++;
    }
    result.resize(j);
    colLen = columns-oldColumns;
    return result;
}

PSynEditFoldRange SynEdit::foldAroundLine(int Line)
{
    return foldAroundLineEx(Line,false,false,false);
}

PSynEditFoldRange SynEdit::foldAroundLineEx(int Line, bool WantCollapsed, bool AcceptFromLine, bool AcceptToLine)
{
    // Check global list
    PSynEditFoldRange Result = checkFoldRange(&mAllFoldRanges, Line, WantCollapsed, AcceptFromLine, AcceptToLine);

    // Found an item in the top level list?
    if (Result) {
        while (true) {
            PSynEditFoldRange ResultChild = checkFoldRange(Result->subFoldRanges.get(), Line, WantCollapsed, AcceptFromLine, AcceptToLine);
            if (!ResultChild)
                break;
            Result = ResultChild; // repeat for this one
        }
    }
    return Result;
}

PSynEditFoldRange SynEdit::checkFoldRange(SynEditFoldRanges *FoldRangeToCheck, int Line, bool WantCollapsed, bool AcceptFromLine, bool AcceptToLine)
{
    for (int i = 0; i< FoldRangeToCheck->count(); i++) {
        PSynEditFoldRange range = (*FoldRangeToCheck)[i];
        if (((range->fromLine < Line) || ((range->fromLine <= Line) && AcceptFromLine)) &&
          ((range->toLine > Line) || ((range->toLine >= Line) && AcceptToLine))) {
            if (range->collapsed == WantCollapsed) {
                return range;
            }
        }
    }
    return PSynEditFoldRange();
}

PSynEditFoldRange SynEdit::foldEndAtLine(int Line)
{
    for (int i = 0; i<mAllFoldRanges.count();i++) {
        PSynEditFoldRange range = mAllFoldRanges[i];
        if (range->toLine == Line ){
            return range;
        } else if (range->fromLine>Line)
            break; // sorted by line. don't bother scanning further
    }
    return PSynEditFoldRange();
}

void SynEdit::paintCaret(QPainter &painter, const QRect rcClip)
{
    if (m_blinkStatus!=1)
        return;
    painter.setClipRect(rcClip);
    SynEditCaretType ct;
    if (this->mInserting) {
        ct = mInsertCaret;
    } else {
        ct =mOverwriteCaret;
    }
    QColor caretColor;
    if (mCaretUseTextColor) {
        caretColor = mForegroundColor;
    } else {
        caretColor = mCaretColor;
    }
    switch(ct) {
    case SynEditCaretType::ctVerticalLine: {
        QRect caretRC;
        int size = std::max(1, mTextHeight/15);
        caretRC.setLeft(rcClip.left()+1);
        caretRC.setTop(rcClip.top());
        caretRC.setBottom(rcClip.bottom());
        caretRC.setRight(rcClip.left()+1+size);
        painter.fillRect(caretRC,caretColor);
        break;
    }
    case SynEditCaretType::ctHorizontalLine: {
        QRect caretRC;
        int size = std::max(1,mTextHeight/15);
        caretRC.setLeft(rcClip.left());
        caretRC.setTop(rcClip.bottom()-1+size);
        caretRC.setBottom(rcClip.bottom()-1);
        caretRC.setRight(rcClip.right());
        painter.fillRect(caretRC,caretColor);
        break;
    }
    case SynEditCaretType::ctBlock:
        painter.fillRect(rcClip, caretColor);
        break;
    case SynEditCaretType::ctHalfBlock:
        QRect rc=rcClip;
        rc.setTop(rcClip.top()+rcClip.height() / 2);
        painter.fillRect(rcClip, caretColor);
        break;
    }
}

int SynEdit::textOffset() const
{
    return mGutterWidth + 2 - (mLeftChar-1)*mCharWidth;
}

SynEditorCommand SynEdit::TranslateKeyCode(int key, Qt::KeyboardModifiers modifiers)
{
    PSynEditKeyStroke keyStroke = mKeyStrokes.findKeycode2(mLastKey,mLastKeyModifiers,
                                                           key, modifiers);
    SynEditorCommand cmd=SynEditorCommand::ecNone;
    if (keyStroke)
        cmd = keyStroke->command();
    else {
        keyStroke = mKeyStrokes.findKeycode(key,modifiers);
        if (keyStroke)
            cmd = keyStroke->command();
    }
    if (cmd == SynEditorCommand::ecNone) {
        mLastKey = key;
        mLastKeyModifiers = modifiers;
    } else {
        mLastKey = 0;
        mLastKeyModifiers = Qt::NoModifier;
    }
    return cmd;
}

void SynEdit::onSizeOrFontChanged(bool bFont)
{

    if (mCharWidth != 0) {
        mCharsInWindow = std::max(clientWidth() - mGutterWidth - 2, 0) / mCharWidth;
        mLinesInWindow = clientHeight() / mTextHeight;
        bool scrollBarChangedSettings = mStateFlags.testFlag(SynStateFlag::sfScrollbarChanged);
        if (bFont) {
            if (mGutter.showLineNumbers())
                onGutterChanged();
            else
                updateScrollbars();
            mStateFlags.setFlag(SynStateFlag::sfCaretChanged,false);
            invalidate();
        } else
            updateScrollbars();
        mStateFlags.setFlag(SynStateFlag::sfScrollbarChanged,scrollBarChangedSettings);
        //if (!mOptions.testFlag(SynEditorOption::eoScrollPastEol))
        setLeftChar(mLeftChar);
        //if (!mOptions.testFlag(SynEditorOption::eoScrollPastEof))
        setTopLine(mTopLine);
    }
}

void SynEdit::onChanged()
{
    emit changed();
}

void SynEdit::onScrolled(int)
{
    mLeftChar = horizontalScrollBar()->value();
    mTopLine = verticalScrollBar()->value();
    invalidate();
}

int SynEdit::mouseSelectionScrollSpeed() const
{
    return mMouseSelectionScrollSpeed;
}

void SynEdit::setMouseSelectionScrollSpeed(int newMouseSelectionScrollSpeed)
{
    mMouseSelectionScrollSpeed = newMouseSelectionScrollSpeed;
}

const QFont &SynEdit::fontForNonAscii() const
{
    return mFontForNonAscii;
}

void SynEdit::setFontForNonAscii(const QFont &newFontForNonAscii)
{
    mFontForNonAscii = newFontForNonAscii;
    mFontForNonAscii.setStyleStrategy(QFont::PreferAntialias);
}

const QColor &SynEdit::backgroundColor() const
{
    return mBackgroundColor;
}

void SynEdit::setBackgroundColor(const QColor &newBackgroundColor)
{
    mBackgroundColor = newBackgroundColor;
}

const QColor &SynEdit::foregroundColor() const
{
    return mForegroundColor;
}

void SynEdit::setForegroundColor(const QColor &newForegroundColor)
{
    mForegroundColor = newForegroundColor;
}

int SynEdit::mouseWheelScrollSpeed() const
{
    return mMouseWheelScrollSpeed;
}

void SynEdit::setMouseWheelScrollSpeed(int newMouseWheelScrollSpeed)
{
    mMouseWheelScrollSpeed = newMouseWheelScrollSpeed;
}

const PSynHighlighterAttribute &SynEdit::rainbowAttr3() const
{
    return mRainbowAttr3;
}

const PSynHighlighterAttribute &SynEdit::rainbowAttr2() const
{
    return mRainbowAttr2;
}

const PSynHighlighterAttribute &SynEdit::rainbowAttr1() const
{
    return mRainbowAttr1;
}

const PSynHighlighterAttribute &SynEdit::rainbowAttr0() const
{
    return mRainbowAttr0;
}

bool SynEdit::caretUseTextColor() const
{
    return mCaretUseTextColor;
}

void SynEdit::setCaretUseTextColor(bool newCaretUseTextColor)
{
    mCaretUseTextColor = newCaretUseTextColor;
}

const QColor &SynEdit::rightEdgeColor() const
{
    return mRightEdgeColor;
}

void SynEdit::setRightEdgeColor(const QColor &newRightEdgeColor)
{
    if (newRightEdgeColor!=mRightEdgeColor) {
        mRightEdgeColor = newRightEdgeColor;
    }
}

int SynEdit::rightEdge() const
{
    return mRightEdge;
}

void SynEdit::setRightEdge(int newRightEdge)
{
    if (mRightEdge != newRightEdge) {
        mRightEdge = newRightEdge;
        invalidate();
    }
}

const QColor &SynEdit::selectedBackground() const
{
    return mSelectedBackground;
}

void SynEdit::setSelectedBackground(const QColor &newSelectedBackground)
{
    mSelectedBackground = newSelectedBackground;
}

const QColor &SynEdit::selectedForeground() const
{
    return mSelectedForeground;
}

void SynEdit::setSelectedForeground(const QColor &newSelectedForeground)
{
    mSelectedForeground = newSelectedForeground;
}

int SynEdit::textHeight() const
{
    return mTextHeight;
}

bool SynEdit::readOnly() const
{
    return mReadOnly;
}

void SynEdit::setReadOnly(bool readOnly)
{
    if (mReadOnly != readOnly) {
        mReadOnly = readOnly;
        emit statusChanged(scReadOnly);
    }
}

SynGutter& SynEdit::gutter()
{
    return mGutter;
}

SynEditCaretType SynEdit::insertCaret() const
{
    return mInsertCaret;
}

void SynEdit::setInsertCaret(const SynEditCaretType &insertCaret)
{
    mInsertCaret = insertCaret;
}

SynEditCaretType SynEdit::overwriteCaret() const
{
    return mOverwriteCaret;
}

void SynEdit::setOverwriteCaret(const SynEditCaretType &overwriteCaret)
{
    mOverwriteCaret = overwriteCaret;
}

QColor SynEdit::activeLineColor() const
{
    return mActiveLineColor;
}

void SynEdit::setActiveLineColor(const QColor &activeLineColor)
{
    if (mActiveLineColor!=activeLineColor) {
        mActiveLineColor = activeLineColor;
        invalidateLine(mCaretY);
    }
}

QColor SynEdit::caretColor() const
{
    return mCaretColor;
}

void SynEdit::setCaretColor(const QColor &caretColor)
{
    mCaretColor = caretColor;
}

void SynEdit::setTabWidth(int newTabWidth)
{
    if (newTabWidth!=tabWidth()) {
        mDocument->setTabWidth(newTabWidth);
        invalidate();
    }
}

SynEditorOptions SynEdit::getOptions() const
{
    return mOptions;
}

void SynEdit::setOptions(const SynEditorOptions &Value)
{
    if (Value != mOptions) {
        //bool bSetDrag = mOptions.testFlag(eoDropFiles) != Value.testFlag(eoDropFiles);
        //if  (!mOptions.testFlag(eoScrollPastEol))
        setLeftChar(mLeftChar);
        //if (!mOptions.testFlag(eoScrollPastEof))
        setTopLine(mTopLine);

        bool bUpdateAll = Value.testFlag(eoShowSpecialChars) != mOptions.testFlag(eoShowSpecialChars);
        if (!bUpdateAll)
            bUpdateAll = Value.testFlag(eoShowRainbowColor) != mOptions.testFlag(eoShowRainbowColor);
        //bool bUpdateScroll = (Options * ScrollOptions)<>(Value * ScrollOptions);
        bool bUpdateScroll = true;
        mOptions = Value;

        // constrain caret position to MaxScrollWidth if eoScrollPastEol is enabled
        internalSetCaretXY(caretXY());
        if (mOptions.testFlag(eoScrollPastEol)) {
            BufferCoord vTempBlockBegin = blockBegin();
            BufferCoord vTempBlockEnd = blockEnd();
            setBlockBegin(vTempBlockBegin);
            setBlockEnd(vTempBlockEnd);
        }
        updateScrollbars();
      // (un)register HWND as drop target
//      if bSetDrag and not (csDesigning in ComponentState) and HandleAllocated then
//        DragAcceptFiles(Handle, (eoDropFiles in fOptions));
        if (bUpdateAll)
            invalidate();
        if (bUpdateScroll)
            updateScrollbars();
    }
}

void SynEdit::doAddStr(const QString &s)
{
    if (mInserting == false && !selAvail()) {
        switch(mActiveSelectionMode) {
        case SynSelectionMode::smColumn: {
            //we can't use blockBegin()/blockEnd()
            BufferCoord start=blockBegin();
            BufferCoord end=blockEnd();
            if (start.Line > end.Line )
                std::swap(start,end);
            start.Char+=s.length(); // make sure we select a whole char in the start line
            setBlockBegin(start);
            setBlockEnd(end);
        }
            break;
        case SynSelectionMode::smLine:
            //do nothing;
            break;
        default:
            setSelLength(s.length());
        }
    }
    doSetSelText(s);
}

void SynEdit::doUndo()
{
    if (mReadOnly)
        return;

    //Remove Group Break;
    if (mUndoList->LastChangeReason() ==  SynChangeReason::crGroupBreak) {
        int OldBlockNumber = mRedoList->blockChangeNumber();
        auto action = finally([&,this]{
           mRedoList->setBlockChangeNumber(OldBlockNumber);
        });
        PSynEditUndoItem Item = mUndoList->PopItem();
        mRedoList->setBlockChangeNumber(Item->changeNumber());
        mRedoList->AddGroupBreak();
    }

    PSynEditUndoItem Item = mUndoList->PeekItem();
    if (Item) {
        int OldChangeNumber = Item->changeNumber();
        int SaveChangeNumber = mRedoList->blockChangeNumber();
        mRedoList->setBlockChangeNumber(Item->changeNumber());
        {
            auto action = finally([&,this] {
               mRedoList->setBlockChangeNumber(SaveChangeNumber);
            });
            //skip group chain breakers
            if (mUndoList->LastChangeReason()==SynChangeReason::crGroupBreak) {
                while (!mUndoList->isEmpty() && mUndoList->LastChangeReason()==SynChangeReason::crGroupBreak) {
                    doUndoItem();
                }
            }
            SynChangeReason  FLastChange = mUndoList->LastChangeReason();
            bool FKeepGoing;
            do {
                doUndoItem();
                Item = mUndoList->PeekItem();
                if (!Item)
                    FKeepGoing = false;
                else {
                    if (Item->changeNumber() == OldChangeNumber)
                        FKeepGoing = true;
                    else {
                        FKeepGoing = (mOptions.testFlag(eoGroupUndo) &&
                            (FLastChange == Item->changeReason()) );
                    }
                    FLastChange = Item->changeReason();
                }
            } while (FKeepGoing);
        }
    }
}

void SynEdit::doUndoItem()
{
    mUndoing = true;
    bool ChangeScrollPastEol = ! mOptions.testFlag(eoScrollPastEol);

    PSynEditUndoItem Item = mUndoList->PopItem();
    if (Item) {
        setActiveSelectionMode(Item->changeSelMode());
        incPaintLock();
        auto action = finally([&,this]{
            mUndoing = false;
            if (ChangeScrollPastEol)
                mOptions.setFlag(eoScrollPastEol,false);
            decPaintLock();
        });
        mOptions.setFlag(eoScrollPastEol);
        switch(Item->changeReason()) {
        case SynChangeReason::crCaret:
            mRedoList->AddChange(
                        Item->changeReason(),
                        caretXY(),
                        caretXY(), QStringList(),
                        Item->changeSelMode());
            internalSetCaretXY(Item->changeStartPos());
            break;
        case SynChangeReason::crLeftTop:
            BufferCoord p;
            p.Char = leftChar();
            p.Line = topLine();
            mRedoList->AddChange(
                        Item->changeReason(),
                        p,
                        p, QStringList(),
                        Item->changeSelMode());
            setLeftChar(Item->changeStartPos().Char);
            setTopLine(Item->changeStartPos().Line);
            break;
        case SynChangeReason::crSelection:
            mRedoList->AddChange(
                        Item->changeReason(),
                        mBlockBegin,
                        mBlockEnd,
                        QStringList(),
                        Item->changeSelMode());
            setCaretAndSelection(caretXY(), Item->changeStartPos(), Item->changeEndPos());
            break;
        case SynChangeReason::crInsert: {
            QStringList tmpText = getContent(Item->changeStartPos(),Item->changeEndPos(),Item->changeSelMode());
            doDeleteText(Item->changeStartPos(),Item->changeEndPos(),Item->changeSelMode());
            mRedoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        tmpText,
                        Item->changeSelMode());
            internalSetCaretXY(Item->changeStartPos());
            break;
        }
        case SynChangeReason::crMoveSelectionUp:
            setBlockBegin(BufferCoord{Item->changeStartPos().Char, Item->changeStartPos().Line-1});
            setBlockEnd(BufferCoord{Item->changeEndPos().Char, Item->changeEndPos().Line-1});
            doMoveSelDown();
            mRedoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        Item->changeText(),
                        Item->changeSelMode());
            break;
        case SynChangeReason::crMoveSelectionDown:
            setBlockBegin(BufferCoord{Item->changeStartPos().Char, Item->changeStartPos().Line+1});
            setBlockEnd(BufferCoord{Item->changeEndPos().Char, Item->changeEndPos().Line+1});
            doMoveSelUp();
            mRedoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        Item->changeText(),
                        Item->changeSelMode());
            break;
        case SynChangeReason::crDelete: {
            // If there's no selection, we have to set
            // the Caret's position manualy.
//            qDebug()<<"undo delete";
//            qDebug()<<Item->changeText();
//            qDebug()<<Item->changeStartPos().Line<<Item->changeStartPos().Char;
            doInsertText(Item->changeStartPos(),Item->changeText(),Item->changeSelMode(),
                         Item->changeStartPos().Line,
                         Item->changeEndPos().Line);
            internalSetCaretXY(Item->changeEndPos());
            mRedoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        Item->changeText(),
                        Item->changeSelMode());
            ensureCursorPosVisible();
            break;
        }
        case SynChangeReason::crLineBreak:{
            QString s;
            if (!Item->changeText().isEmpty()) {
                s=Item->changeText()[0];
            }
            // If there's no selection, we have to set
            // the Caret's position manualy.
            internalSetCaretXY(Item->changeStartPos());
            if (mCaretY > 0) {
                QString TmpStr = mDocument->getString(mCaretY - 1);
                if ( (mCaretX > TmpStr.length() + 1) && (leftSpaces(s) == 0))
                    TmpStr = TmpStr + QString(mCaretX - 1 - TmpStr.length(), ' ');
                properSetLine(mCaretY - 1, TmpStr + s);
                mDocument->deleteAt(mCaretY);
                doLinesDeleted(mCaretY, 1);
            }
            mRedoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        QStringList(),
                        Item->changeSelMode());
            break;
        }
        default:
            break;
        }
    }
}

void SynEdit::doRedo()
{
    if (mReadOnly)
        return;

    PSynEditUndoItem Item = mRedoList->PeekItem();
    if (!Item)
        return;
    int OldChangeNumber = Item->changeNumber();
    int SaveChangeNumber = mUndoList->blockChangeNumber();
    mUndoList->setBlockChangeNumber(Item->changeNumber());
    {
        auto action = finally([&,this]{
            mUndoList->setBlockChangeNumber(SaveChangeNumber);
        });
        //skip group chain breakers
        if (mRedoList->LastChangeReason()==SynChangeReason::crGroupBreak) {
            while (!mRedoList->isEmpty() && mRedoList->LastChangeReason()==SynChangeReason::crGroupBreak) {
                doRedoItem();
            }
        }
        SynChangeReason FLastChange = mRedoList->LastChangeReason();
        bool FKeepGoing;
        do {
          doRedoItem();
          Item = mRedoList->PeekItem();
          if (!Item)
              FKeepGoing = false;
          else {
            if (Item->changeNumber() == OldChangeNumber)
                FKeepGoing = true;
            else {
                FKeepGoing = (mOptions.testFlag(eoGroupUndo) &&
                (FLastChange == Item->changeReason()));
            }
            FLastChange = Item->changeReason();
          }
        } while (FKeepGoing);

    }
    //Remove Group Break
    if (mRedoList->LastChangeReason() == SynChangeReason::crGroupBreak) {
        int OldBlockNumber = mUndoList->blockChangeNumber();
        Item = mRedoList->PopItem();
        {
            auto action2=finally([&,this]{
                mUndoList->setBlockChangeNumber(OldBlockNumber);
            });
            mUndoList->setBlockChangeNumber(Item->changeNumber());
            mUndoList->AddGroupBreak();
        }
        updateModifiedStatus();
    }
}

void SynEdit::doRedoItem()
{
    mUndoing = true;
    bool ChangeScrollPastEol = ! mOptions.testFlag(eoScrollPastEol);
    PSynEditUndoItem Item = mRedoList->PopItem();
    if (Item) {
        setActiveSelectionMode(Item->changeSelMode());
        incPaintLock();
        mOptions.setFlag(eoScrollPastEol);
        mUndoList->setInsideRedo(true);
        auto action = finally([&,this]{
            mUndoing = false;
            mUndoList->setInsideRedo(false);
            if (ChangeScrollPastEol)
                mOptions.setFlag(eoScrollPastEol,false);
            decPaintLock();
        });
        switch(Item->changeReason()) {
        case SynChangeReason::crCaret:
            mUndoList->AddChange(
                        Item->changeReason(),
                        caretXY(),
                        caretXY(),
                        QStringList(),
                        mActiveSelectionMode);
            internalSetCaretXY(Item->changeStartPos());
            break;
        case SynChangeReason::crLeftTop:
            BufferCoord p;
            p.Char = leftChar();
            p.Line = topLine();
            mUndoList->AddChange(
                        Item->changeReason(),
                        p,
                        p, QStringList(),
                        Item->changeSelMode());
            setLeftChar(Item->changeStartPos().Char);
            setTopLine(Item->changeStartPos().Line);
            break;
        case SynChangeReason::crSelection:
            mUndoList->AddChange(
                        Item->changeReason(),
                        mBlockBegin,
                        mBlockEnd,
                        QStringList(),
                        mActiveSelectionMode);
            setCaretAndSelection(
                        caretXY(),
                        Item->changeStartPos(),
                        Item->changeEndPos());
            break;
        case SynChangeReason::crMoveSelectionUp:
            setBlockBegin(BufferCoord{Item->changeStartPos().Char, Item->changeStartPos().Line});
            setBlockEnd(BufferCoord{Item->changeEndPos().Char, Item->changeEndPos().Line});
            doMoveSelUp();
            mUndoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        Item->changeText(),
                        Item->changeSelMode());
            break;
        case SynChangeReason::crMoveSelectionDown:
            setBlockBegin(BufferCoord{Item->changeStartPos().Char, Item->changeStartPos().Line});
            setBlockEnd(BufferCoord{Item->changeEndPos().Char, Item->changeEndPos().Line});
            doMoveSelDown();
            mUndoList->AddChange(
                        Item->changeReason(),
                        Item->changeStartPos(),
                        Item->changeEndPos(),
                        Item->changeText(),
                        Item->changeSelMode());
            break;
        case SynChangeReason::crInsert:
            setCaretAndSelection(
                        Item->changeStartPos(),
                        Item->changeStartPos(),
                        Item->changeStartPos());
            doInsertText(Item->changeStartPos(),Item->changeText(), Item->changeSelMode(),
                         Item->changeStartPos().Line,
                         Item->changeEndPos().Line);
            internalSetCaretXY(Item->changeEndPos());
            mUndoList->AddChange(Item->changeReason(),
                                 Item->changeStartPos(),
                                 Item->changeEndPos(),
                                 QStringList(),
                                 Item->changeSelMode());
            break;
        case SynChangeReason::crDelete: {
            doDeleteText(Item->changeStartPos(),Item->changeEndPos(),Item->changeSelMode());
            mUndoList->AddChange(Item->changeReason(), Item->changeStartPos(),
                                 Item->changeEndPos(),Item->changeText(),
                                 Item->changeSelMode());
            internalSetCaretXY(Item->changeStartPos());
            break;
        };
        case SynChangeReason::crLineBreak: {
            BufferCoord CaretPt = Item->changeStartPos();
            setCaretAndSelection(CaretPt, CaretPt, CaretPt);
            commandProcessor(SynEditorCommand::ecLineBreak);
            break;
        }
        default:
            break;
        }
    }
}

void SynEdit::doZoomIn()
{
    QFont newFont = font();
    int size = newFont.pixelSize();
    size++;
    newFont.setPixelSize(size);
    setFont(newFont);
}

void SynEdit::doZoomOut()
{
    QFont newFont = font();
    int size = newFont.pixelSize();
    size--;
    if (size<2)
        size = 2;
    newFont.setPixelSize(size);
    setFont(newFont);
}

SynSelectionMode SynEdit::selectionMode() const
{
    return mSelectionMode;
}

void SynEdit::setSelectionMode(SynSelectionMode value)
{
    if (mSelectionMode!=value) {
        mSelectionMode = value;
        setActiveSelectionMode(value);
    }
}

QString SynEdit::selText()
{
    if (!selAvail()) {
        return "";
    } else {
        int ColFrom = blockBegin().Char;
        int First = blockBegin().Line - 1;
        //
        int ColTo = blockEnd().Char;
        int Last = blockEnd().Line - 1;

        switch(mActiveSelectionMode) {
        case SynSelectionMode::smNormal:{
            PSynEditFoldRange foldRange = foldStartAtLine(blockEnd().Line);
            QString s = mDocument->getString(Last);
            if ((foldRange) && foldRange->collapsed && ColTo>s.length()) {
                s=s+highlighter()->foldString();
                if (ColTo>s.length()) {
                    Last = foldRange->toLine-1;
                    ColTo = mDocument->getString(Last).length()+1;
                }
            }
            if (First == Last)
                return  mDocument->getString(First).mid(ColFrom-1, ColTo - ColFrom);
            else {
                QString result = mDocument->getString(First).mid(ColFrom-1);
                result+= lineBreak();
                for (int i = First + 1; i<=Last - 1; i++) {
                    result += mDocument->getString(i);
                    result+=lineBreak();
                }
                result += mDocument->getString(Last).leftRef(ColTo-1);
                return result;
            }
        }
        case SynSelectionMode::smColumn:
        {
              First = blockBegin().Line;
              ColFrom = charToColumn(blockBegin().Line, blockBegin().Char);
              Last = blockEnd().Line;
              ColTo = charToColumn(blockEnd().Line, blockEnd().Char);
              if (ColFrom > ColTo)
                  std::swap(ColFrom, ColTo);
              if (First>Last)
                  std::swap(First,Last);
              QString result;
              for (int i = First; i <= Last; i++) {
                  int l = columnToChar(i,ColFrom);
                  int r = columnToChar(i,ColTo-1)+1;
                  QString s = mDocument->getString(i-1);
                  result += s.mid(l-1,r-l);
                  if (i<Last)
                      result+=lineBreak();
              }
              return result;
        }
        case SynSelectionMode::smLine:
        {
            QString result;
            // If block selection includes LastLine,
            // line break code(s) of the last line will not be added.
            for (int i= First; i<=Last - 1;i++) {
                result += mDocument->getString(i);
                result+=lineBreak();
            }
            result += mDocument->getString(Last);
            if (Last < mDocument->count() - 1)
                result+=lineBreak();
            return result;
        }
        }
    }
    return "";
}

QStringList SynEdit::getContent(BufferCoord startPos, BufferCoord endPos, SynSelectionMode mode) const
{
    QStringList result;
    if (startPos==endPos) {
        return result;
    }
    if (startPos>endPos) {
        std::swap(startPos,endPos);
    }
    int ColFrom = startPos.Char;
    int First = startPos.Line - 1;
    //
    int ColTo = endPos.Char;
    int Last = endPos.Line - 1;

    switch(mode) {
    case SynSelectionMode::smNormal:{
        PSynEditFoldRange foldRange = foldStartAtLine(endPos.Line);
        QString s = mDocument->getString(Last);
        if ((foldRange) && foldRange->collapsed && ColTo>s.length()) {
            s=s+highlighter()->foldString();
            if (ColTo>s.length()) {
                Last = foldRange->toLine-1;
                ColTo = mDocument->getString(Last).length()+1;
            }
        }
    }
        if (First == Last) {
            result.append(mDocument->getString(First).mid(ColFrom-1, ColTo - ColFrom));
        } else {
            result.append(mDocument->getString(First).mid(ColFrom-1));
            for (int i = First + 1; i<=Last - 1; i++) {
                result.append(mDocument->getString(i));
            }
            result.append(mDocument->getString(Last).left(ColTo-1));
        }
        break;
    case SynSelectionMode::smColumn:
          First = blockBegin().Line;
          ColFrom = charToColumn(blockBegin().Line, blockBegin().Char);
          Last = blockEnd().Line;
          ColTo = charToColumn(blockEnd().Line, blockEnd().Char);
          if (ColFrom > ColTo)
              std::swap(ColFrom, ColTo);
          if (First>Last)
              std::swap(First,Last);
          for (int i = First; i <= Last; i++) {
              int l = columnToChar(i,ColFrom);
              int r = columnToChar(i,ColTo-1)+1;
              QString s = mDocument->getString(i-1);
              result.append(s.mid(l-1,r-l));
          }
          break;
    case SynSelectionMode::smLine:
        // If block selection includes LastLine,
        // line break code(s) of the last line will not be added.
        for (int i= First; i<=Last - 1;i++) {
            result.append(mDocument->getString(i));
        }
        result.append(mDocument->getString(Last));
        if (Last < mDocument->count() - 1)
            result.append("");
        break;
    }
    return result;
}

QString SynEdit::lineBreak()
{
    return mDocument->lineBreak();
}

bool SynEdit::useCodeFolding() const
{
    return mUseCodeFolding;
}

void SynEdit::setUseCodeFolding(bool value)
{
    if (mUseCodeFolding!=value) {
        mUseCodeFolding = value;
    }
}

SynEditCodeFolding &SynEdit::codeFolding()
{
    return mCodeFolding;
}

QString SynEdit::displayLineText()
{
    if (mCaretY >= 1 && mCaretY <= mDocument->count()) {
        QString s= mDocument->getString(mCaretY - 1);
        PSynEditFoldRange foldRange = foldStartAtLine(mCaretY);
        if ((foldRange) && foldRange->collapsed) {
            return s+highlighter()->foldString();
        }
        return s;
    }
    return QString();
}

QString SynEdit::lineText() const
{
    if (mCaretY >= 1 && mCaretY <= mDocument->count())
        return mDocument->getString(mCaretY - 1);
    else
        return QString();
}

void SynEdit::setLineText(const QString s)
{
    if (mCaretY >= 1 && mCaretY <= mDocument->count())
        mDocument->putString(mCaretY-1,s);
}

PSynHighlighter SynEdit::highlighter() const
{
    return mHighlighter;
}

void SynEdit::setHighlighter(const PSynHighlighter &highlighter)
{
    PSynHighlighter oldHighlighter= mHighlighter;
    mHighlighter = highlighter;
    if (oldHighlighter && mHighlighter &&
            oldHighlighter->language() == highlighter->language()) {
    } else {
        recalcCharExtent();
        mDocument->beginUpdate();
        auto action=finally([this]{
            mDocument->endUpdate();
        });
        rescanRanges();
    }
    onSizeOrFontChanged(true);
    invalidate();
}

const PSynDocument& SynEdit::document() const
{
    return mDocument;
}

bool SynEdit::empty()
{
    return mDocument->empty();
}

void SynEdit::commandProcessor(SynEditorCommand Command, QChar AChar, void *pData)
{
    // first the program event handler gets a chance to process the command
    onProcessCommand(Command, AChar, pData);
    if (Command != SynEditorCommand::ecNone)
        ExecuteCommand(Command, AChar, pData);
    onCommandProcessed(Command, AChar, pData);
}

void SynEdit::moveCaretHorz(int DX, bool isSelection)
{
    BufferCoord ptO = caretXY();
    BufferCoord ptDst = ptO;
    QString s = displayLineText();
    int nLineLen = s.length();
    // only moving or selecting one char can change the line
    //bool bChangeY = !mOptions.testFlag(SynEditorOption::eoScrollPastEol);
    bool bChangeY=true;
    if (bChangeY && (DX == -1) && (ptO.Char == 1) && (ptO.Line > 1)) {
        // end of previous line
        if (mActiveSelectionMode==SynSelectionMode::smColumn) {
            return;
        }
        int row = lineToRow(ptDst.Line);
        row--;
        int line = rowToLine(row);
        if (line!=ptDst.Line && line>=1) {
            ptDst.Line = line;
            ptDst.Char = getDisplayStringAtLine(ptDst.Line).length() + 1;
        }
    } else if (bChangeY && (DX == 1) && (ptO.Char > nLineLen) && (ptO.Line < mDocument->count())) {
        // start of next line
        if (mActiveSelectionMode==SynSelectionMode::smColumn) {
            return;
        }
        int row = lineToRow(ptDst.Line);
        row++;
        int line = rowToLine(row);
//        qDebug()<<line<<ptDst.Line;
        if (line!=ptDst.Line && line<=mDocument->count()) {
            ptDst.Line = line;
            ptDst.Char = 1;
        }
    } else {
        ptDst.Char = std::max(1, ptDst.Char + DX);
        // don't go past last char when ScrollPastEol option not set
        if ((DX > 0) && bChangeY)
          ptDst.Char = std::min(ptDst.Char, nLineLen + 1);
    }
    // set caret and block begin / end
    incPaintLock();
    if (mOptions.testFlag(eoAltSetsColumnMode) &&
                         (mActiveSelectionMode != SynSelectionMode::smLine)) {
        if (qApp->keyboardModifiers().testFlag(Qt::AltModifier)) {
            setActiveSelectionMode(SynSelectionMode::smColumn);
        } else
            setActiveSelectionMode(selectionMode());
    }
    moveCaretAndSelection(mBlockBegin, ptDst, isSelection);
    decPaintLock();
}

void SynEdit::moveCaretVert(int DY, bool isSelection)
{
    DisplayCoord ptO = displayXY();
    DisplayCoord ptDst = ptO;


    ptDst.Row+=DY;
    if (DY >= 0) {
        if (rowToLine(ptDst.Row) > mDocument->count())
            ptDst.Row = std::max(1, displayLineCount());
    } else {
        if (ptDst.Row < 1)
            ptDst.Row = 1;
    }

    if (ptO.Row != ptDst.Row) {
        if (mOptions.testFlag(eoKeepCaretX))
            ptDst.Column = mLastCaretColumn;
    }
    BufferCoord vDstLineChar = displayToBufferPos(ptDst);

    if (mActiveSelectionMode==SynSelectionMode::smColumn) {
        QString s=mDocument->getString(vDstLineChar.Line-1);
        int cols=stringColumns(s,0);
        if (cols+1<ptO.Column)
            return;
    }

    int SaveLastCaretX = mLastCaretColumn;

    // set caret and block begin / end
    incPaintLock();
    if (mOptions.testFlag(eoAltSetsColumnMode) &&
                         (mActiveSelectionMode != SynSelectionMode::smLine)) {
        if (qApp->keyboardModifiers().testFlag(Qt::AltModifier))
            setActiveSelectionMode(SynSelectionMode::smColumn);
        else
            setActiveSelectionMode(selectionMode());
    }
    moveCaretAndSelection(mBlockBegin, vDstLineChar, isSelection);
    decPaintLock();

    // Restore fLastCaretX after moving caret, since
    // UpdateLastCaretX, called by SetCaretXYEx, changes them. This is the one
    // case where we don't want that.
    mLastCaretColumn = SaveLastCaretX;
}

void SynEdit::moveCaretAndSelection(const BufferCoord &ptBefore, const BufferCoord &ptAfter, bool isSelection)
{
    if (mOptions.testFlag(SynEditorOption::eoGroupUndo) && mUndoList->CanUndo())
      mUndoList->AddGroupBreak();

    incPaintLock();
    if (isSelection) {

        if (!selAvail())
          setBlockBegin(ptBefore);
        setBlockEnd(ptAfter);
    } else
        setBlockBegin(ptAfter);
    internalSetCaretXY(ptAfter);
    decPaintLock();
}

void SynEdit::moveCaretToLineStart(bool isSelection)
{
    int newX;
    // home key enhancement
    if (mOptions.testFlag(SynEditorOption::eoEnhanceHomeKey)) {
        QString s = mDocument->getString(mCaretY - 1);

        int first_nonblank = 0;
        int vMaxX = s.length();
        while ((first_nonblank < vMaxX) && (s[first_nonblank] == ' ' || s[first_nonblank] == '\t')) {
            first_nonblank++;
        }
        newX = mCaretX;

        if ((newX > first_nonblank+1)
                || (newX == 1))
            newX = first_nonblank+1;
        else
            newX = 1;
    } else
        newX = 1;
    moveCaretAndSelection(caretXY(), BufferCoord{newX, mCaretY}, isSelection);
}

void SynEdit::moveCaretToLineEnd(bool isSelection)
{
    int vNewX;
    if (mOptions.testFlag(SynEditorOption::eoEnhanceEndKey)) {
        QString vText = displayLineText();
        int vLastNonBlank = vText.length()-1;
        int vMinX = 0;
        while ((vLastNonBlank >= vMinX) && (vText[vLastNonBlank] == ' ' || vText[vLastNonBlank] =='\t'))
            vLastNonBlank--;
        vLastNonBlank++;
        vNewX = mCaretX;
        if ((vNewX <= vLastNonBlank) || (vNewX == vText.length() + 1))
            vNewX = vLastNonBlank + 1;
        else
            vNewX = vText.length() + 1;
    } else
        vNewX = displayLineText().length() + 1;

    moveCaretAndSelection(caretXY(), BufferCoord{vNewX, mCaretY}, isSelection);
}

void SynEdit::setSelectedTextEmpty()
{
    BufferCoord startPos=blockBegin();
    BufferCoord endPos=blockEnd();
    doDeleteText(startPos,endPos,mActiveSelectionMode);
    internalSetCaretXY(startPos);
}

void SynEdit::setSelTextPrimitive(const QStringList &text)
{
    setSelTextPrimitiveEx(mActiveSelectionMode, text);
}

void SynEdit::setSelTextPrimitiveEx(SynSelectionMode mode, const QStringList &text)
{
    incPaintLock();
    bool groupUndo=false;
    BufferCoord startPos = blockBegin();
    BufferCoord endPos = blockEnd();
    if (selAvail()) {
        if (!mUndoing && !text.isEmpty()) {
            mUndoList->BeginBlock();
            groupUndo=true;
        }
        doDeleteText(startPos,endPos,activeSelectionMode());
        if (mode == SynSelectionMode::smColumn) {
            int colBegin = charToColumn(startPos.Line,startPos.Char);
            int colEnd = charToColumn(endPos.Line,endPos.Char);
            if (colBegin<colEnd)
                internalSetCaretXY(startPos);
            else
                internalSetCaretXY(endPos);
        } else
            internalSetCaretXY(startPos);
    }
    if (!text.isEmpty()) {
        doInsertText(caretXY(),text,mode,mBlockBegin.Line,mBlockEnd.Line);
    }
    if (groupUndo) {
        mUndoList->EndBlock();
    }
    decPaintLock();
    setStatusChanged(SynStatusChange::scSelection);
}

void SynEdit::doSetSelText(const QString &value)
{
    bool blockBeginned = false;
    auto action = finally([this, &blockBeginned]{
        if (blockBeginned)
            mUndoList->EndBlock();
    });
    if (selAvail()) {
      mUndoList->BeginBlock();
      blockBeginned = true;
//      mUndoList->AddChange(
//                  SynChangeReason::crDelete, mBlockBegin, mBlockEnd,
//                  selText(), mActiveSelectionMode);
    }
//    } else if (!colSelAvail())
//        setActiveSelectionMode(selectionMode());
    BufferCoord StartOfBlock = blockBegin();
    BufferCoord EndOfBlock = blockEnd();
    mBlockBegin = StartOfBlock;
    mBlockEnd = EndOfBlock;
    setSelTextPrimitive(splitStrings(value));
}

int SynEdit::searchReplace(const QString &sSearch, const QString &sReplace, SynSearchOptions sOptions, PSynSearchBase searchEngine,
                    SynSearchMathedProc matchedCallback, SynSearchConfirmAroundProc confirmAroundCallback)
{
    if (!searchEngine)
        return 0;

    // can't search for or replace an empty string
    if (sSearch.isEmpty()) {
        return 0;
    }
    int result = 0;
    // get the text range to search in, ignore the "Search in selection only"
    // option if nothing is selected
    bool bBackward = sOptions.testFlag(ssoBackwards);
    bool bFromCursor = !sOptions.testFlag(ssoEntireScope);
    BufferCoord ptCurrent;
    BufferCoord ptStart;
    BufferCoord ptEnd;
    if (!selAvail())
        sOptions.setFlag(ssoSelectedOnly,false);
    if (sOptions.testFlag(ssoSelectedOnly)) {
        ptStart = blockBegin();
        ptEnd = blockEnd();
        // search the whole line in the line selection mode
        if (mActiveSelectionMode == SynSelectionMode::smLine) {
            ptStart.Char = 1;
            ptEnd.Char = mDocument->getString(ptEnd.Line - 1).length();
        } else if (mActiveSelectionMode == SynSelectionMode::smColumn) {
            // make sure the start column is smaller than the end column
            if (ptStart.Char > ptEnd.Char)
                std::swap(ptStart.Char,ptEnd.Char);
        }
        // ignore the cursor position when searching in the selection
        if (bBackward) {
            ptCurrent = ptEnd;
        } else {
            ptCurrent = ptStart;
        }
    } else {
        ptStart.Char = 1;
        ptStart.Line = 1;
        ptEnd.Line = mDocument->count();
        ptEnd.Char = mDocument->getString(ptEnd.Line - 1).length();
        if (bFromCursor) {
            if (bBackward)
                ptEnd = caretXY();
            else
                ptStart = caretXY();
        }
        if (bBackward)
            ptCurrent = ptEnd;
        else
            ptCurrent = ptStart;
    }
    BufferCoord originCaretXY=caretXY();
    // initialize the search engine
    searchEngine->setOptions(sOptions);
    searchEngine->setPattern(sSearch);
    // search while the current search position is inside of the search range
    bool dobatchReplace = false;
    doOnPaintTransient(SynTransientType::ttBefore);
    {
        auto action = finally([&,this]{
            if (dobatchReplace) {
                decPaintLock();
                mUndoList->EndBlock();
            }
            doOnPaintTransient(SynTransientType::ttAfter);
        });
        int i;
        // If it's a search only we can leave the procedure now.
        SynSearchAction searchAction = SynSearchAction::Exit;
        while ((ptCurrent.Line >= ptStart.Line) && (ptCurrent.Line <= ptEnd.Line)) {
            int nInLine = searchEngine->findAll(mDocument->getString(ptCurrent.Line - 1));
            int iResultOffset = 0;
            if (bBackward)
                i = searchEngine->resultCount()-1;
            else
                i = 0;
            // Operate on all results in this line.
            while (nInLine > 0) {
                // An occurrence may have been replaced with a text of different length
                int nFound = searchEngine->result(i) + 1 + iResultOffset;
                int nSearchLen = searchEngine->length(i);
                int nReplaceLen = 0;
                if (bBackward)
                    i--;
                else
                    i++;
                nInLine--;
                // Is the search result entirely in the search range?
                bool isInValidSearchRange = true;
                int first = nFound;
                int last = nFound + nSearchLen;
                if ((mActiveSelectionMode == SynSelectionMode::smNormal)
                        || !sOptions.testFlag(ssoSelectedOnly)) {
                    if (((ptCurrent.Line == ptStart.Line) && (first < ptStart.Char)) ||
                            ((ptCurrent.Line == ptEnd.Line) && (last > ptEnd.Char)))
                        isInValidSearchRange = false;
                } else if (mActiveSelectionMode == SynSelectionMode::smColumn) {
                    // solves bug in search/replace when smColumn mode active and no selection
                    isInValidSearchRange = ((first >= ptStart.Char) && (last <= ptEnd.Char))
                            || (ptEnd.Char - ptStart.Char < 1);
                }
                if (!isInValidSearchRange)
                    continue;
                result++;
                // Select the text, so the user can see it in the OnReplaceText event
                // handler or as the search result.
                ptCurrent.Char = nFound;
                setBlockBegin(ptCurrent);

                //Be sure to use the Ex version of CursorPos so that it appears in the middle if necessary
                setCaretXYEx(false, BufferCoord{1, ptCurrent.Line});
                ensureCursorPosVisibleEx(true);
                ptCurrent.Char += nSearchLen;
                setBlockEnd(ptCurrent);
                //internalSetCaretXY(ptCurrent);
                if (bBackward)
                    internalSetCaretXY(blockBegin());
                else
                    internalSetCaretXY(ptCurrent);

                QString replaceText = searchEngine->replace(selText(), sReplace);
                if (matchedCallback && !dobatchReplace) {
                    searchAction = matchedCallback(sSearch,replaceText,ptCurrent.Line,
                                    nFound,nSearchLen);
                }
                if (searchAction==SynSearchAction::Exit) {
                    return result;
                } else if (searchAction == SynSearchAction::Skip) {
                    continue;
                } else if (searchAction == SynSearchAction::Replace
                           || searchAction == SynSearchAction::ReplaceAll) {
                    if (!dobatchReplace &&
                            (searchAction == SynSearchAction::ReplaceAll) ){
                        incPaintLock();
                        mUndoList->BeginBlock();
                        dobatchReplace = true;
                    }
                    bool oldAutoIndent = mOptions.testFlag(SynEditorOption::eoAutoIndent);
                    mOptions.setFlag(SynEditorOption::eoAutoIndent,false);
                    doSetSelText(replaceText);
                    nReplaceLen = caretX() - nFound;
                    // fix the caret position and the remaining results
                    if (!bBackward) {
                        internalSetCaretX(nFound + nReplaceLen);
                        if ((nSearchLen != nReplaceLen)) {
                            iResultOffset += nReplaceLen - nSearchLen;
                            if ((mActiveSelectionMode != SynSelectionMode::smColumn) && (caretY() == ptEnd.Line)) {
                                ptEnd.Char+=nReplaceLen - nSearchLen;
                                setBlockEnd(ptEnd);
                            }
                        }
                    }
                    mOptions.setFlag(SynEditorOption::eoAutoIndent,oldAutoIndent);
                }
            }
            // search next / previous line
            if (bBackward)
                ptCurrent.Line--;
            else
                ptCurrent.Line++;
            if (((ptCurrent.Line < ptStart.Line) || (ptCurrent.Line > ptEnd.Line))
                    && bFromCursor && sOptions.testFlag(ssoWrapAround)){
                if (confirmAroundCallback && !confirmAroundCallback())
                    break;
                //search start from cursor, search has finished but no result founds
                bFromCursor = false;
                ptStart.Char = 1;
                ptStart.Line = 1;
                ptEnd.Line = mDocument->count();
                ptEnd.Char = mDocument->getString(ptEnd.Line - 1).length();
                if (bBackward) {
                    ptStart = originCaretXY;
                    ptCurrent = ptEnd;
                } else {
                    ptEnd= originCaretXY;
                    ptCurrent = ptStart;
                }
            }
        }
    }
    return result;
}

void SynEdit::doLinesDeleted(int firstLine, int count)
{
    emit linesDeleted(firstLine, count);
//    // gutter marks
//    for i := 0 to Marks.Count - 1 do begin
//      if Marks[i].Line >= FirstLine + Count then
//        Marks[i].Line := Marks[i].Line - Count
//      else if Marks[i].Line > FirstLine then
//        Marks[i].Line := FirstLine;
//    end;
//    // plugins
//    if fPlugins <> nil then begin
//      for i := 0 to fPlugins.Count - 1 do
//        TSynEditPlugin(fPlugins[i]).LinesDeleted(FirstLine, Count);
    //    end;
}

void SynEdit::doLinesInserted(int firstLine, int count)
{
    emit linesInserted(firstLine, count);
//    // gutter marks
//    for i := 0 to Marks.Count - 1 do begin
//      if Marks[i].Line >= FirstLine then
//        Marks[i].Line := Marks[i].Line + Count;
//    end;
//    // plugins
//    if fPlugins <> nil then begin
//      for i := 0 to fPlugins.Count - 1 do
//        TSynEditPlugin(fPlugins[i]).LinesInserted(FirstLine, Count);
//    end;
}

void SynEdit::properSetLine(int ALine, const QString &ALineText, bool notify)
{
    if (mOptions.testFlag(eoTrimTrailingSpaces)) {
        mDocument->putString(ALine,trimRight(ALineText),notify);
    } else {
        mDocument->putString(ALine,ALineText,notify);
    }
}

void SynEdit::doDeleteText(BufferCoord startPos, BufferCoord endPos, SynSelectionMode mode)
{
    bool UpdateMarks = false;
    int MarkOffset = 0;
    if (mode == SynSelectionMode::smNormal) {
        PSynEditFoldRange foldRange = foldStartAtLine(endPos.Line);
        QString s = mDocument->getString(endPos.Line-1);
        if ((foldRange) && foldRange->collapsed && endPos.Char>s.length()) {
            QString newS=s+highlighter()->foldString();
            if ((startPos.Char<=s.length() || startPos.Line<endPos.Line)
                    && endPos.Char>newS.length() ) {
                //selection has whole block
                endPos.Line = foldRange->toLine;
                endPos.Char = mDocument->getString(endPos.Line-1).length()+1;
            } else {
                return;
            }
        }
    }
    QStringList deleted=getContent(startPos,endPos,mode);
    switch(mode) {
    case SynSelectionMode::smNormal:
        if (mDocument->count() > 0) {
            // Create a string that contains everything on the first line up
            // to the selection mark, and everything on the last line after
            // the selection mark.
            QString TempString = mDocument->getString(startPos.Line - 1).mid(0, startPos.Char - 1)
                + mDocument->getString(endPos.Line - 1).mid(endPos.Char-1);
//            bool collapsed=foldCollapsedBetween(BB.Line,BE.Line);
            // Delete all lines in the selection range.
            mDocument->deleteLines(startPos.Line, endPos.Line - startPos.Line);
            properSetLine(startPos.Line-1,TempString);
            UpdateMarks = true;
            internalSetCaretXY(startPos);
//            if (collapsed) {
//                PSynEditFoldRange foldRange = foldStartAtLine(BB.Line);
//                if (!foldRange
//                        || (!foldRange->collapsed))
//                    uncollapseAroundLine(BB.Line);
//            }
        }
        break;
    case SynSelectionMode::smColumn:
    {
        int First = startPos.Line - 1;
        int ColFrom = charToColumn(startPos.Line, startPos.Char);
        int Last = endPos.Line - 1;
        int ColTo = charToColumn(endPos.Line, endPos.Char);
        if (ColFrom > ColTo)
            std::swap(ColFrom, ColTo);
        if (First > Last)
            std::swap(First,Last);
        QString result;
        for (int i = First; i <= Last; i++) {
            int l = columnToChar(i+1,ColFrom);
            int r = columnToChar(i+1,ColTo-1)+1;
            QString s = mDocument->getString(i);
            s.remove(l-1,r-l);
            properSetLine(i,s);
        }
        // Lines never get deleted completely, so keep caret at end.
        internalSetCaretXY(startPos);
        // Column deletion never removes a line entirely, so no mark
        // updating is needed here.
        break;
    }
    case SynSelectionMode::smLine:
        if (endPos.Line == mDocument->count()) {
            mDocument->putString(endPos.Line - 1,"");
            mDocument->deleteLines(startPos.Line-1,endPos.Line-startPos.Line);
        } else {
            mDocument->deleteLines(startPos.Line-1,endPos.Line-startPos.Line+1);
        }
        // smLine deletion always resets to first column.
        internalSetCaretXY(BufferCoord{1, startPos.Line});
        UpdateMarks = true;
        MarkOffset = 1;
        break;
    }
    // Update marks
    if (UpdateMarks)
        doLinesDeleted(startPos.Line, endPos.Line - startPos.Line + MarkOffset);
    if (!mUndoing) {
        mUndoList->AddChange(SynChangeReason::crDelete,
                             startPos,
                             endPos,
                             deleted,
                             mode);
    }
}

void SynEdit::doInsertText(const BufferCoord& pos,
                           const QStringList& text,
                           SynSelectionMode mode, int startLine, int endLine) {
    if (text.isEmpty())
        return;
    if (startLine>endLine)
        std::swap(startLine,endLine);

    if (mode == SynSelectionMode::smNormal) {
        PSynEditFoldRange foldRange = foldStartAtLine(pos.Line);
        QString s = mDocument->getString(pos.Line-1);
        if ((foldRange) && foldRange->collapsed && pos.Char>s.length()+1)
            return;
    }
    int insertedLines = 0;
    BufferCoord newPos;
    switch(mode){
    case SynSelectionMode::smNormal:
        insertedLines = doInsertTextByNormalMode(pos,text, newPos);
        doLinesInserted(pos.Line+1, insertedLines);
        break;
    case SynSelectionMode::smColumn:
        insertedLines = doInsertTextByColumnMode(pos,text, newPos, startLine,endLine);
        doLinesInserted(endLine-insertedLines+1,insertedLines);
        break;
    case SynSelectionMode::smLine:
        insertedLines = doInsertTextByLineMode(pos,text, newPos);
        doLinesInserted(pos.Line, insertedLines);
        break;
    }
    internalSetCaretXY(newPos);
    ensureCursorPosVisible();
}

int SynEdit::doInsertTextByNormalMode(const BufferCoord& pos, const QStringList& text, BufferCoord &newPos)
{
    QString sLeftSide;
    QString sRightSide;
    QString str;
    bool bChangeScroll;
//    int SpaceCount;
    int result = 0;
    int startLine = pos.Line;
    QString line=mDocument->getString(pos.Line-1);
    sLeftSide = line.mid(0, pos.Char - 1);
    if (pos.Char - 1 > sLeftSide.length()) {
        if (stringIsBlank(sLeftSide))
            sLeftSide = GetLeftSpacing(displayX() - 1, true);
        else
            sLeftSide += QString(pos.Char - 1 - sLeftSide.length(),' ');
    }
    sRightSide = line.mid(pos.Char - 1);
//    if (mUndoing) {
//        SpaceCount = 0;
//    } else {
//        SpaceCount = leftSpaces(sLeftSide);
//    }
    int caretY=pos.Line;
    // step1: insert the first line of Value into current line
    if (text.length()>1) {
        if (!mUndoing && mHighlighter && mOptions.testFlag(eoAutoIndent)) {
            QString s = trimLeft(text[0]);
            if (sLeftSide.isEmpty()) {
                sLeftSide = GetLeftSpacing(calcIndentSpaces(caretY,s,true),true);
            }
            str = sLeftSide + s;
        } else
            str = sLeftSide + text[0];
        properSetLine(caretY - 1, str);
        mDocument->insertLines(caretY, text.length()-1);
    } else {
        str = sLeftSide + text[0] + sRightSide;
        properSetLine(caretY - 1, str);
    }
    rescanRange(caretY);
    // step2: insert remaining lines of Value
    for (int i=1;i<text.length();i++) {
        bool notInComment = true;
//        if (mHighlighter) {
//            notInComment = !mHighlighter->isLastLineCommentNotFinished(
//                    mHighlighter->getRangeState().state)
//                && !mHighlighter->isLastLineStringNotFinished(
//                    mHighlighter->getRangeState().state);
//        }
        caretY=pos.Line+i;
//        mStatusChanges.setFlag(SynStatusChange::scCaretY);
        if (text[i].isEmpty()) {
            if (i==text.length()-1) {
                str = sRightSide;
            } else {
                if (!mUndoing && mHighlighter && mOptions.testFlag(eoAutoIndent) && notInComment) {
                    str = GetLeftSpacing(calcIndentSpaces(caretY,"",true),true);
                } else {
                    str = "";
                }
            }
        } else {
            str = text[i];
            if (i==text.length()-1)
                str += sRightSide;
            if (!mUndoing && mHighlighter && mOptions.testFlag(eoAutoIndent) && notInComment) {
                int indentSpaces = calcIndentSpaces(caretY,str,true);
                str = GetLeftSpacing(indentSpaces,true)+trimLeft(str);
            }
        }
        properSetLine(caretY - 1, str,false);
        rescanRange(caretY);
        result++;
    }
    bChangeScroll = !mOptions.testFlag(eoScrollPastEol);
    mOptions.setFlag(eoScrollPastEol);
    auto action = finally([&,this]{
        if (bChangeScroll)
            mOptions.setFlag(eoScrollPastEol,false);
    });
    if (mOptions.testFlag(eoTrimTrailingSpaces) && (sRightSide == "")) {
        newPos=BufferCoord{mDocument->getString(caretY-1).length()+1,caretY};
    } else
        newPos=BufferCoord{str.length() - sRightSide.length()+1,caretY};
    onLinesPutted(startLine-1,result+1);
    if (!mUndoing) {
        mUndoList->AddChange(
                    SynChangeReason::crInsert,
                    pos,newPos,
                    QStringList(),SynSelectionMode::smNormal);
    }
    return result;
}

int SynEdit::doInsertTextByColumnMode(const BufferCoord& pos, const QStringList& text, BufferCoord &newPos, int startLine, int endLine)
{
    QString str;
    QString tempString;
    int line;
    int len;
    BufferCoord  lineBreakPos;
    int result = 0;
    DisplayCoord insertCoord = bufferToDisplayPos(caretXY());
    int insertCol = insertCoord.Column;
    line = startLine;
    if (!mUndoing) {
        mUndoList->BeginBlock();
    }
    int i=0;
    while(line<=endLine) {
        str = text[i];
        int insertPos = 0;
        if (line > mDocument->count()) {
            result++;
            tempString = QString(insertCol - 1,' ') + str;
            mDocument->add("");
            if (!mUndoing) {
                result++;
                lineBreakPos.Line = line - 1;
                lineBreakPos.Char = mDocument->getString(line - 2).length() + 1;
                mUndoList->AddChange(SynChangeReason::crLineBreak,
                                 lineBreakPos,
                                 lineBreakPos,
                                 QStringList(), SynSelectionMode::smNormal);
            }
        } else {
            tempString = mDocument->getString(line - 1);
            len = stringColumns(tempString,0);
            if (len < insertCol) {
                insertPos = tempString.length()+1;
                tempString = tempString + QString(insertCol - len - 1,' ') + str;
            } else {
                insertPos = columnToChar(line,insertCol);
                tempString.insert(insertPos-1,str);
            }
        }
        properSetLine(line - 1, tempString);
        // Add undo change here from PasteFromClipboard
        if (!mUndoing) {
            mUndoList->AddChange(
                        SynChangeReason::crInsert,
                        BufferCoord{insertPos, line},
                        BufferCoord{insertPos+str.length(), line},
                        QStringList(),
                        SynSelectionMode::smNormal);
        }
        if (i<text.length()-1) {
            i++;
        }
        line++;
    }
    newPos=pos;
    if (!text[0].isEmpty()) {
        newPos.Char+=text[0].length();
//        mCaretX+=firstLineLen;
//        mStatusChanges.setFlag(SynStatusChange::scCaretX);
    }
    if (!mUndoing) {
        mUndoList->EndBlock();
    }
    return result;
}

int SynEdit::doInsertTextByLineMode(const BufferCoord& pos, const QStringList& text, BufferCoord &newPos)
{
    QString Str;
    int Result = 0;
    newPos=pos;
    newPos.Char=1;
//    mCaretX = 1;
//    emit statusChanged(SynStatusChange::scCaretX);
    // Insert string before current line
    for (int i=0;i<text.length();i++) {
        if ((mCaretY == mDocument->count()) || mInserting) {
            mDocument->insert(mCaretY - 1, "");
            Result++;
        }
        properSetLine(mCaretY - 1, Str);
        newPos.Line++;
//        mCaretY++;
//        mStatusChanges.setFlag(SynStatusChange::scCaretY);
    }
    if (!mUndoing) {
        mUndoList->AddChange(
                    SynChangeReason::crInsert,
                    BufferCoord{1,pos.Line},newPos,
                    QStringList(),SynSelectionMode::smLine);
    }
    return Result;
}

void SynEdit::deleteFromTo(const BufferCoord &start, const BufferCoord &end)
{
    if (mReadOnly)
        return;
    doOnPaintTransient(SynTransientType::ttBefore);
    if ((start.Char != end.Char) || (start.Line != end.Line)) {
        BufferCoord oldCaret = caretXY();
        mUndoList->BeginBlock();
        mUndoList->AddChange(SynChangeReason::crCaret, oldCaret, start,
                             QStringList(), activeSelectionMode());
        mUndoList->AddChange(SynChangeReason::crSelection,
                             mBlockBegin,
                             mBlockEnd,
                             QStringList(),activeSelectionMode());
        setBlockBegin(start);
        setBlockEnd(end);
        doDeleteText(start,end,SynSelectionMode::smNormal);
        mUndoList->EndBlock();
        internalSetCaretXY(start);
    }
    doOnPaintTransient(SynTransientType::ttAfter);
}

bool SynEdit::onGetSpecialLineColors(int, QColor &, QColor &)
{
    return false;
}

void SynEdit::onGetEditingAreas(int, SynEditingAreaList &)
{

}

void SynEdit::onGutterGetText(int , QString &)
{

}

void SynEdit::onGutterPaint(QPainter &, int , int , int )
{

}

void SynEdit::onPaint(QPainter &)
{

}

void SynEdit::onPreparePaintHighlightToken(int , int , const QString &,
                                           PSynHighlighterAttribute , SynFontStyles &, QColor &, QColor &)
{

}

void SynEdit::onProcessCommand(SynEditorCommand , QChar , void *)
{

}

void SynEdit::onCommandProcessed(SynEditorCommand , QChar , void *)
{

}

void SynEdit::ExecuteCommand(SynEditorCommand Command, QChar AChar, void *pData)
{
    hideCaret();
    incPaintLock();

    auto action=finally([this] {
        decPaintLock();
        showCaret();
    });
    switch(Command) {
    //horizontal caret movement or selection
    case SynEditorCommand::ecLeft:
    case SynEditorCommand::ecSelLeft:
        moveCaretHorz(-1, Command == SynEditorCommand::ecSelLeft);
        break;
    case SynEditorCommand::ecRight:
    case SynEditorCommand::ecSelRight:
        moveCaretHorz(1, Command == SynEditorCommand::ecSelRight);
        break;
    case SynEditorCommand::ecPageLeft:
    case SynEditorCommand::ecSelPageLeft:
        moveCaretHorz(-mCharsInWindow, Command == SynEditorCommand::ecSelPageLeft);
        break;
    case SynEditorCommand::ecPageRight:
    case SynEditorCommand::ecSelPageRight:
        moveCaretHorz(mCharsInWindow, Command == SynEditorCommand::ecSelPageRight);
        break;
    case SynEditorCommand::ecLineStart:
    case SynEditorCommand::ecSelLineStart:
        moveCaretToLineStart(Command == SynEditorCommand::ecSelLineStart);
        break;
    case SynEditorCommand::ecLineEnd:
    case SynEditorCommand::ecSelLineEnd:
        moveCaretToLineEnd(Command == SynEditorCommand::ecSelLineEnd);
        break;
    // vertical caret movement or selection
    case SynEditorCommand::ecUp:
    case SynEditorCommand::ecSelUp:
        moveCaretVert(-1, Command == SynEditorCommand::ecSelUp);
        break;
    case SynEditorCommand::ecDown:
    case SynEditorCommand::ecSelDown:
        moveCaretVert(1, Command == SynEditorCommand::ecSelDown);
        break;
    case SynEditorCommand::ecPageUp:
    case SynEditorCommand::ecSelPageUp:
    case SynEditorCommand::ecPageDown:
    case SynEditorCommand::ecSelPageDown:
    {
        int counter = mLinesInWindow;
        if (mOptions.testFlag(eoHalfPageScroll))
            counter /= 2;
        if (mOptions.testFlag(eoScrollByOneLess)) {
            counter -=1;
        }
        if (counter<0)
            break;
        if (Command == SynEditorCommand::ecPageUp || Command == SynEditorCommand::ecSelPageUp) {
            counter = -counter;
        }
        moveCaretVert(counter, Command == SynEditorCommand::ecSelPageUp || Command == SynEditorCommand::ecSelPageDown);
        break;
    }
    case SynEditorCommand::ecPageTop:
    case SynEditorCommand::ecSelPageTop:
        moveCaretVert(mTopLine-mCaretY, Command == SynEditorCommand::ecSelPageTop);
        break;
    case SynEditorCommand::ecPageBottom:
    case SynEditorCommand::ecSelPageBottom:
        moveCaretVert(mTopLine+mLinesInWindow-1-mCaretY, Command == SynEditorCommand::ecSelPageBottom);
        break;
    case SynEditorCommand::ecEditorStart:
    case SynEditorCommand::ecSelEditorStart:
        moveCaretVert(1-mCaretY, Command == SynEditorCommand::ecSelEditorStart);
        moveCaretToLineStart(Command == SynEditorCommand::ecSelEditorStart);
        break;
    case SynEditorCommand::ecEditorEnd:
    case SynEditorCommand::ecSelEditorEnd:
        if (!mDocument->empty()) {
            moveCaretVert(mDocument->count()-mCaretY, Command == SynEditorCommand::ecSelEditorEnd);
            moveCaretToLineEnd(Command == SynEditorCommand::ecSelEditorEnd);
        }
        break;
    // goto special line / column position
    case SynEditorCommand::ecGotoXY:
    case SynEditorCommand::ecSelGotoXY:
        if (pData)
            moveCaretAndSelection(caretXY(), *((BufferCoord *)(pData)), Command == SynEditorCommand::ecSelGotoXY);
        break;
    // word selection
    case SynEditorCommand::ecWordLeft:
    case SynEditorCommand::ecSelWordLeft:
    {
        BufferCoord CaretNew = prevWordPos();
        moveCaretAndSelection(caretXY(), CaretNew, Command == SynEditorCommand::ecSelWordLeft);
        break;
    }
    case SynEditorCommand::ecWordRight:
    case SynEditorCommand::ecSelWordRight:
    {
        BufferCoord CaretNew = nextWordPos();
        moveCaretAndSelection(caretXY(), CaretNew, Command == SynEditorCommand::ecSelWordRight);
        break;
    }
    case SynEditorCommand::ecSelWord:
        setSelWord();
        break;
    case SynEditorCommand::ecSelectAll:
        doSelectAll();
        break;
    case SynEditorCommand::ecDeleteLastChar:
        doDeleteLastChar();
        break;
    case SynEditorCommand::ecDeleteChar:
        doDeleteCurrentChar();
        break;
    case SynEditorCommand::ecDeleteWord:
        doDeleteWord();
        break;
    case SynEditorCommand::ecDeleteEOL:
        doDeleteToEOL();
        break;
    case SynEditorCommand::ecDeleteWordStart:
        doDeleteToWordStart();
        break;
    case SynEditorCommand::ecDeleteWordEnd:
        doDeleteToWordEnd();
        break;
    case SynEditorCommand::ecDeleteBOL:
        doDeleteFromBOL();
        break;
    case SynEditorCommand::ecDeleteLine:
        doDeleteLine();
        break;
    case SynEditorCommand::ecDuplicateLine:
        doDuplicateLine();
        break;
    case SynEditorCommand::ecMoveSelUp:
        doMoveSelUp();
        break;
    case SynEditorCommand::ecMoveSelDown:
        doMoveSelDown();
        break;
    case SynEditorCommand::ecClearAll:
        clearAll();
        break;
    case SynEditorCommand::ecInsertLine:
        insertLine(false);
        break;
    case SynEditorCommand::ecLineBreak:
        insertLine(true);
        break;
    case SynEditorCommand::ecLineBreakAtEnd:
        mUndoList->BeginBlock();
        mUndoList->AddChange(
                    SynChangeReason::crCaret,
                    caretXY(),
                    caretXY(),
                    QStringList(),
                    activeSelectionMode());
        moveCaretToLineEnd(false);
        insertLine(true);
        mUndoList->EndBlock();
        break;
    case SynEditorCommand::ecTab:
        doTabKey();
        break;
    case SynEditorCommand::ecShiftTab:
        doShiftTabKey();
        break;
    case SynEditorCommand::ecChar:
        doAddChar(AChar);
        break;
    case SynEditorCommand::ecInsertMode:
        if (!mReadOnly)
            setInsertMode(true);
        break;
    case SynEditorCommand::ecOverwriteMode:
        if (!mReadOnly)
            setInsertMode(false);
        break;
    case SynEditorCommand::ecToggleMode:
        if (!mReadOnly) {
            setInsertMode(!mInserting);
        }
        break;
    case SynEditorCommand::ecCut:
        if (!mReadOnly)
            doCutToClipboard();
        break;
    case SynEditorCommand::ecCopy:
        doCopyToClipboard();
        break;
    case SynEditorCommand::ecPaste:
        if (!mReadOnly)
            doPasteFromClipboard();
        break;
    case SynEditorCommand::ecImeStr:
    case SynEditorCommand::ecString:
        if (!mReadOnly)
            doAddStr(*((QString*)pData));
        break;
    case SynEditorCommand::ecUndo:
        if (!mReadOnly)
            doUndo();
        break;
    case SynEditorCommand::ecRedo:
        if (!mReadOnly)
            doRedo();
        break;
    case SynEditorCommand::ecZoomIn:
        doZoomIn();
        break;
    case SynEditorCommand::ecZoomOut:
        doZoomOut();
        break;
    case SynEditorCommand::ecComment:
        doComment();
        break;
    case SynEditorCommand::ecUncomment:
        doUncomment();
        break;
    case SynEditorCommand::ecToggleComment:
        doToggleComment();
        break;
    case SynEditorCommand::ecToggleBlockComment:
        doToggleBlockComment();
        break;
    case SynEditorCommand::ecNormalSelect:
        setSelectionMode(SynSelectionMode::smNormal);
        break;
    case SynEditorCommand::ecLineSelect:
        setSelectionMode(SynSelectionMode::smLine);
        break;
    case SynEditorCommand::ecColumnSelect:
        setSelectionMode(SynSelectionMode::smColumn);
        break;
    case SynEditorCommand::ecScrollLeft:
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()-mMouseWheelScrollSpeed);
        break;
    case SynEditorCommand::ecScrollRight:
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()+mMouseWheelScrollSpeed);
        break;
    case SynEditorCommand::ecScrollUp:
        verticalScrollBar()->setValue(verticalScrollBar()->value()-mMouseWheelScrollSpeed);
        break;
    case SynEditorCommand::ecScrollDown:
        verticalScrollBar()->setValue(verticalScrollBar()->value()+mMouseWheelScrollSpeed);
        break;
    case SynEditorCommand::ecMatchBracket:
        {
        BufferCoord coord = getMatchingBracket();
        if (coord.Char!=0 && coord.Line!=0)
            internalSetCaretXY(coord);
        }
        break;
    default:
        break;
    }


}

void SynEdit::onEndFirstPaintLock()
{

}

void SynEdit::onBeginFirstPaintLock()
{

}

bool SynEdit::isIdentChar(const QChar &ch)
{
    if (mHighlighter) {
        return mHighlighter->isIdentChar(ch);
    } else {
        if (ch == '_') {
            return true;
        }
        if ((ch>='0') && (ch <= '9')) {
            return true;
        }
        if ((ch>='a') && (ch <= 'z')) {
            return true;
        }
        if ((ch>='A') && (ch <= 'Z')) {
            return true;
        }
        return false;
    }
}

void SynEdit::setRainbowAttrs(const PSynHighlighterAttribute &attr0, const PSynHighlighterAttribute &attr1, const PSynHighlighterAttribute &attr2, const PSynHighlighterAttribute &attr3)
{
    mRainbowAttr0 = attr0;
    mRainbowAttr1 = attr1;
    mRainbowAttr2 = attr2;
    mRainbowAttr3 = attr3;
}

void SynEdit::updateMouseCursor(){
    QPoint p = mapFromGlobal(cursor().pos());
    if (p.y() >= clientHeight() || p.x()>= clientWidth()) {
        setCursor(Qt::ArrowCursor);
    } else if (p.x() > mGutterWidth) {
        setCursor(Qt::IBeamCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

bool SynEdit::isCaretVisible()
{
    if (mCaretY < mTopLine)
        return false;
    if (mCaretY >= mTopLine + mLinesInWindow )
        return false;
    if (mCaretX < mLeftChar)
        return false;
    if (mCaretX >= mLeftChar + mCharsInWindow)
        return false;
    return true;
}

void SynEdit::paintEvent(QPaintEvent *event)
{
    if (mPainterLock>0)
        return;
    if (mPainting)
        return;
    mPainting = true;
    auto action = finally([&,this] {
        mPainting = false;
    });

    // Now paint everything while the caret is hidden.
    QPainter painter(viewport());
    //Get the invalidated rect.
    QRect rcClip = event->rect();
    QRect rcCaret = calculateCaretRect();

    if (rcCaret == rcClip) {
        // only update caret
        // calculate the needed invalid area for caret
        //qDebug()<<"update caret"<<rcCaret;
        QRectF cacheRC;
        qreal dpr = mContentImage->devicePixelRatioF();
        cacheRC.setLeft(rcClip.left()*dpr);
        cacheRC.setTop(rcClip.top()*dpr);
        cacheRC.setWidth(rcClip.width()*dpr);
        cacheRC.setHeight(rcClip.height()*dpr);
        painter.drawImage(rcCaret,*mContentImage,cacheRC);
    } else {
        QRect rcDraw;
        int nL1, nL2, nC1, nC2;
        // Compute the invalid area in lines / columns.
        // columns
        nC1 = mLeftChar;
        if (rcClip.left() > mGutterWidth + 2 )
            nC1 += (rcClip.left() - mGutterWidth - 2 ) / mCharWidth;
        nC2 = mLeftChar +
          (rcClip.right() - mGutterWidth - 2 + mCharWidth - 1) / mCharWidth;
        // lines
        nL1 = minMax(mTopLine + rcClip.top() / mTextHeight, mTopLine, displayLineCount());
        nL2 = minMax(mTopLine + (rcClip.bottom() + mTextHeight - 1) / mTextHeight, 1, displayLineCount());

        //qDebug()<<"Paint:"<<nL1<<nL2<<nC1<<nC2;

        QPainter cachePainter(mContentImage.get());
        cachePainter.setFont(font());
        SynEditTextPainter textPainter(this, &cachePainter,
                                       nL1,nL2,nC1,nC2);
        // First paint paint the text area if it was (partly) invalidated.
        if (rcClip.right() > mGutterWidth ) {
            rcDraw = rcClip;
            rcDraw.setLeft( std::max(rcDraw.left(), mGutterWidth));
            textPainter.paintTextLines(rcDraw);
        }

        // Then the gutter area if it was (partly) invalidated.
        if (rcClip.left() < mGutterWidth) {
            rcDraw = rcClip;
            rcDraw.setRight(mGutterWidth-1);
            textPainter.paintGutter(rcDraw);
        }

        //PluginsAfterPaint(Canvas, rcClip, nL1, nL2);
        // If there is a custom paint handler call it.
        onPaint(painter);
        doOnPaintTransient(SynTransientType::ttAfter);
        QRectF cacheRC;
        qreal dpr = mContentImage->devicePixelRatioF();
        cacheRC.setLeft(rcClip.left()*dpr);
        cacheRC.setTop(rcClip.top()*dpr);
        cacheRC.setWidth(rcClip.width()*dpr);
        cacheRC.setHeight(rcClip.height()*dpr);
        painter.drawImage(rcClip,*mContentImage,cacheRC);
    }
    paintCaret(painter, rcCaret);
}

void SynEdit::resizeEvent(QResizeEvent *)
{
    //resize the cache image
    qreal dpr = devicePixelRatioF();
    std::shared_ptr<QImage> image = std::make_shared<QImage>(clientWidth()*dpr,clientHeight()*dpr,
                                                            QImage::Format_ARGB32);
    image->setDevicePixelRatio(dpr);
    QRect newRect = image->rect().intersected(mContentImage->rect());

    QPainter painter(image.get());

    painter.drawImage(newRect,*mContentImage);

    mContentImage = image;

    onSizeOrFontChanged(false);
}

void SynEdit::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_blinkTimerId) {
        m_blinkStatus = 1- m_blinkStatus;
        updateCaret();
    }
}

bool SynEdit::event(QEvent *event)
{
    switch(event->type()) {
    case QEvent::KeyPress:{
        QKeyEvent* keyEvent = static_cast<QKeyEvent *>(event);
        if(keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
        {
            // process tab key presse event
            keyPressEvent(keyEvent);
            return true;
        }
    }
        break;
    case QEvent::FontChange:
        synFontChanged();
        if (mDocument)
            mDocument->setFontMetrics(font());
        break;
    case QEvent::MouseMove: {
        updateMouseCursor();
        break;
    }
    default:
        break;
    }
    return QAbstractScrollArea::event(event);
}

void SynEdit::focusInEvent(QFocusEvent *)
{
    showCaret();
}

void SynEdit::focusOutEvent(QFocusEvent *)
{
    hideCaret();
}

void SynEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && mActiveSelectionMode != mSelectionMode) {
        setActiveSelectionMode(selectionMode());
        setBlockBegin(caretXY());
        setBlockEnd(caretXY());
        event->accept();
    } else {
        SynEditorCommand cmd=TranslateKeyCode(event->key(),event->modifiers());
        if (cmd!=SynEditorCommand::ecNone) {
            commandProcessor(cmd,QChar(),nullptr);
            event->accept();
        } else if (!event->text().isEmpty()) {
            QChar c = event->text().at(0);
            if (c=='\t' || c.isPrint()) {
                commandProcessor(SynEditorCommand::ecChar,c,nullptr);
                event->accept();
            }
        }
    }
    if (!event->isAccepted()) {
        QAbstractScrollArea::keyPressEvent(event);
    }
}

void SynEdit::mousePressEvent(QMouseEvent *event)
{
    bool bWasSel = false;
    bool bStartDrag = false;
    mMouseMoved = false;
    Qt::MouseButton button = event->button();
    int X=event->pos().x();
    int Y=event->pos().y();

    QAbstractScrollArea::mousePressEvent(event);


    if (button == Qt::RightButton) {
        if (mOptions.testFlag(eoRightMouseMovesCursor) &&
                ( (selAvail() && ! isPointInSelection(displayToBufferPos(pixelsToRowColumn(X, Y))))
                  || ! selAvail())) {
            invalidateSelection();
            mBlockEnd=mBlockBegin;
            computeCaret();
        }else {
            return;
        }
    } else if (button == Qt::LeftButton) {
        if (selAvail()) {
            //remember selection state, as it will be cleared later
            bWasSel = true;
            mMouseDownPos = event->pos();
        }
        computeCaret();
        mStateFlags.setFlag(SynStateFlag::sfWaitForDragging,false);
        if (bWasSel && mOptions.testFlag(eoDragDropEditing) && (X >= mGutterWidth + 2)
                && (mSelectionMode == SynSelectionMode::smNormal) && isPointInSelection(displayToBufferPos(pixelsToRowColumn(X, Y))) ) {
            bStartDrag = true;
        }
        if (bStartDrag) {
            mStateFlags.setFlag(SynStateFlag::sfWaitForDragging);
        } else {
            if (event->modifiers() == Qt::ShiftModifier) {
                //BlockBegin and BlockEnd are restored to their original position in the
                //code from above and SetBlockEnd will take care of proper invalidation
                setBlockEnd(caretXY());
            } else if (mOptions.testFlag(eoAltSetsColumnMode) &&
                     (mActiveSelectionMode != SynSelectionMode::smLine)) {
                if (event->modifiers() == Qt::AltModifier)
                    setActiveSelectionMode(SynSelectionMode::smColumn);
                else
                    setActiveSelectionMode(selectionMode());
                //Selection mode must be set before calling SetBlockBegin
                setBlockBegin(caretXY());
            }
            computeScroll(false);
        }
    }
}

void SynEdit::mouseReleaseEvent(QMouseEvent *event)
{
    QAbstractScrollArea::mouseReleaseEvent(event);
    int X=event->pos().x();
    /* int Y=event->pos().y(); */

    if (!mMouseMoved && (X < mGutterWidth + 2)) {
        processGutterClick(event);
    }


    if (mStateFlags.testFlag(SynStateFlag::sfWaitForDragging) &&
            !mStateFlags.testFlag(SynStateFlag::sfDblClicked)) {
        computeCaret();
        if (! (event->modifiers() & Qt::ShiftModifier))
            setBlockBegin(caretXY());
        setBlockEnd(caretXY());
        mStateFlags.setFlag(SynStateFlag::sfWaitForDragging, false);
    }
    mStateFlags.setFlag(SynStateFlag::sfDblClicked,false);
}

void SynEdit::mouseMoveEvent(QMouseEvent *event)
{
    QAbstractScrollArea::mouseMoveEvent(event);
    mMouseMoved = true;
    Qt::MouseButtons buttons = event->buttons();
    if ((mStateFlags.testFlag(SynStateFlag::sfWaitForDragging))) {
        if ( ( event->pos() - mMouseDownPos).manhattanLength()>=QApplication::startDragDistance()) {
            mStateFlags.setFlag(SynStateFlag::sfWaitForDragging,false);
            QDrag *drag = new QDrag(this);
            QMimeData *mimeData = new QMimeData;

            mimeData->setText(selText());
            drag->setMimeData(mimeData);

            drag->exec(Qt::CopyAction | Qt::MoveAction);
        }
    } else if ((buttons == Qt::LeftButton)) {
        if (mOptions.testFlag(eoAltSetsColumnMode) &&
                (mActiveSelectionMode != SynSelectionMode::smLine)) {
                if (event->modifiers() == Qt::AltModifier)
                    setActiveSelectionMode(SynSelectionMode::smColumn);
                else
                    setActiveSelectionMode(selectionMode());
        }
    } else if (buttons == Qt::NoButton) {
        updateMouseCursor();
    }
}

void SynEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    QPoint ptMouse = event->pos();
    if (ptMouse.x() >= mGutterWidth + 2) {
          setWordBlock(caretXY());
          mStateFlags.setFlag(SynStateFlag::sfDblClicked);
    }
}

void SynEdit::inputMethodEvent(QInputMethodEvent *event)
{
//    qDebug()<<event->replacementStart()<<":"<<event->replacementLength()<<" - "
//           << event->preeditString()<<" - "<<event->commitString();

    QString oldString = mInputPreeditString;
    mInputPreeditString = event->preeditString();
    if (oldString!=mInputPreeditString) {
        if (mActiveSelectionMode==SynSelectionMode::smColumn) {
            BufferCoord selBegin = blockBegin();
            BufferCoord selEnd = blockEnd();
            invalidateLines(selBegin.Line,selEnd.Line);
        } else
            invalidateLine(mCaretY);
    }
    QString s = event->commitString();
    if (!s.isEmpty()) {
        commandProcessor(SynEditorCommand::ecImeStr,QChar(),&s);
//        for (QChar ch:s) {
//            CommandProcessor(SynEditorCommand::ecChar,ch);
//        }
    }
}

void SynEdit::leaveEvent(QEvent *)
{
    setCursor(Qt::ArrowCursor);
}

void SynEdit::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() == Qt::ShiftModifier) {
        if (event->angleDelta().y()>0) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value()-mMouseWheelScrollSpeed);
            event->accept();
            return;
        } else if (event->angleDelta().y()<0) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value()+mMouseWheelScrollSpeed);
            event->accept();
            return;
        }
    } else {
        if (event->angleDelta().y()>0) {
            verticalScrollBar()->setValue(verticalScrollBar()->value()-mMouseWheelScrollSpeed);
            event->accept();
            return;
        } else if (event->angleDelta().y()<0) {
            verticalScrollBar()->setValue(verticalScrollBar()->value()+mMouseWheelScrollSpeed);
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(event);
}

bool SynEdit::viewportEvent(QEvent * event)
{
//    switch (event->type()) {
//        case QEvent::Resize:
//            sizeOrFontChanged(false);
//        break;
//    }
    return QAbstractScrollArea::viewportEvent(event);
}

QVariant SynEdit::inputMethodQuery(Qt::InputMethodQuery property) const
{
    QRect rect = calculateInputCaretRect();

    switch(property) {
    case Qt::ImCursorRectangle:
        return rect;
    default:
        return QWidget::inputMethodQuery(property);
    }

}

void SynEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("text/plain")) {
        event->acceptProposedAction();
        mDragCaretSave = caretXY();
        mDragSelBeginSave = blockBegin();
        mDragSelEndSave = blockEnd();
        BufferCoord coord = displayToBufferPos(pixelsToNearestRowColumn(event->pos().x(),
                                                                        event->pos().y()));
        internalSetCaretXY(coord);
        setBlockBegin(mDragSelBeginSave);
        setBlockEnd(mDragSelEndSave);
        showCaret();
        computeScroll(true);
    }
}

void SynEdit::dropEvent(QDropEvent *event)
{
    //mScrollTimer->stop();

    BufferCoord coord = displayToBufferPos(pixelsToNearestRowColumn(event->pos().x(),
                                                                    event->pos().y()));
    if (coord>=mDragSelBeginSave && coord<=mDragSelEndSave) {
        //do nothing if drag onto itself
        event->acceptProposedAction();
        mDropped = true;
        return;
    }
    int topLine = mTopLine;
    int leftChar = mLeftChar;
    QStringList text=splitStrings(event->mimeData()->text());
    mUndoList->BeginBlock();
    addLeftTopToUndo();
    addCaretToUndo();
    addSelectionToUndo();
    internalSetCaretXY(coord);
    if (event->proposedAction() == Qt::DropAction::CopyAction) {
        //just copy it
        doInsertText(coord,text,mActiveSelectionMode,coord.Line,coord.Line+text.length()-1);
    } else if (event->proposedAction() == Qt::DropAction::MoveAction)  {
        if (coord < mDragSelBeginSave ) {
            //delete old
            doDeleteText(mDragSelBeginSave,mDragSelEndSave,mActiveSelectionMode);
            //paste to new position
            doInsertText(coord,text,mActiveSelectionMode,coord.Line,coord.Line+text.length()-1);
        } else {
            //paste to new position
            doInsertText(coord,text,mActiveSelectionMode,coord.Line,coord.Line+text.length()-1);
            //delete old
            doDeleteText(mDragSelBeginSave,mDragSelEndSave,mActiveSelectionMode);
            //set caret to right pos
            if (mDragSelBeginSave.Line == mDragSelEndSave.Line) {
                if (coord.Line == mDragSelEndSave.Line) {
                    coord.Char -= mDragSelEndSave.Char-mDragSelBeginSave.Char;
                }
            } else {
                if (coord.Line == mDragSelEndSave.Line) {
                    coord.Char -= mDragSelEndSave.Char-1;
                } else {
                    coord.Line -= mDragSelEndSave.Line-mDragSelBeginSave.Line;
                    topLine -= mDragSelEndSave.Line-mDragSelBeginSave.Line;
                }
            }
        }
        mUndoList->EndBlock();

    }
    event->acceptProposedAction();
    mDropped = true;
    setTopLine(topLine);
    setLeftChar(leftChar);
    internalSetCaretXY(coord);
}

void SynEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->keyboardModifiers() ==  Qt::ControlModifier) {
        event->setDropAction(Qt::CopyAction);
    } else {
        event->setDropAction(Qt::MoveAction);
    }
    // should we begin scrolling?
//    computeScroll(event->pos().x(),
//                  event->pos().y(),true);

    QPoint iMousePos = QCursor::pos();
    iMousePos = mapFromGlobal(iMousePos);
    int X=iMousePos.x();
    int Y=iMousePos.y();
    BufferCoord coord = displayToBufferPos(pixelsToNearestRowColumn(X,Y));
    internalSetCaretXY(coord);
    setBlockBegin(mDragSelBeginSave);
    setBlockEnd(mDragSelEndSave);
    showCaret();
}

void SynEdit::dragLeaveEvent(QDragLeaveEvent *)
{
//    setCaretXY(mDragCaretSave);
//    setBlockBegin(mDragSelBeginSave);
//    setBlockEnd(mDragSelEndSave);
    //    showCaret();
}

int SynEdit::maxScrollHeight() const
{
    if (mOptions.testFlag(eoScrollPastEof))
        return std::max(displayLineCount(),1);
    else
        return std::max(displayLineCount()-mLinesInWindow+1, 1);
}

bool SynEdit::modified() const
{
    return mModified;
}

void SynEdit::setModified(bool Value)
{
    if (Value) {
        mLastModifyTime = QDateTime::currentDateTime();
        emit statusChanged(SynStatusChange::scModified);
    }
    if (Value != mModified) {
        mModified = Value;
        if (mOptions.testFlag(SynEditorOption::eoGroupUndo) && (!Value) && mUndoList->CanUndo())
            mUndoList->AddGroupBreak();
        mUndoList->setInitialState(!Value);
        emit statusChanged(SynStatusChange::scModifyChanged);
    }
}

int SynEdit::gutterWidth() const
{
    return mGutterWidth;
}

void SynEdit::setGutterWidth(int Value)
{
    Value = std::max(Value, 0);
    if (mGutterWidth != Value) {
        mGutterWidth = Value;
        onSizeOrFontChanged(false);
        invalidate();
    }
}

int SynEdit::charWidth() const
{
    return mCharWidth;
}

void SynEdit::setUndoLimit(int size)
{
    mUndoList->setMaxUndoActions(size);

    mRedoList->setMaxUndoActions(size);
}

int SynEdit::charsInWindow() const
{
    return mCharsInWindow;
}

void SynEdit::onBookMarkOptionsChanged()
{
    invalidateGutter();
}

void SynEdit::onLinesChanged()
{
    SynSelectionMode vOldMode;
    mStateFlags.setFlag(SynStateFlag::sfLinesChanging, false);

    updateScrollbars();
    if (mActiveSelectionMode == SynSelectionMode::smColumn) {
        BufferCoord oldBlockStart = blockBegin();
        BufferCoord oldBlockEnd = blockEnd();
        oldBlockStart.Char = mCaretX;
        int colEnd = charToColumn(oldBlockStart.Line,oldBlockStart.Char);
        int charEnd = columnToChar(oldBlockEnd.Line,colEnd);
        oldBlockEnd.Char =  charEnd;
        setBlockBegin(oldBlockStart);
        setBlockEnd(oldBlockEnd);
    } else {
        vOldMode = mActiveSelectionMode;
        setBlockBegin(caretXY());
        mActiveSelectionMode = vOldMode;
    }
    if (mInvalidateRect.width()==0)
        invalidate();
    else
        invalidateRect(mInvalidateRect);
    mInvalidateRect = {0,0,0,0};
    if (mGutter.showLineNumbers() && (mGutter.autoSize()))
        mGutter.autoSizeDigitCount(mDocument->count());
    //if (!mOptions.testFlag(SynEditorOption::eoScrollPastEof))
    setTopLine(mTopLine);
}

void SynEdit::onLinesChanging()
{
    mStateFlags.setFlag(SynStateFlag::sfLinesChanging);
}

void SynEdit::onLinesCleared()
{
    if (mUseCodeFolding)
        foldOnListCleared();
    clearUndo();
    // invalidate the *whole* client area
    mInvalidateRect={0,0,0,0};
    invalidate();
    // set caret and selected block to start of text
    setCaretXY({1,1});
    // scroll to start of text
    setTopLine(1);
    setLeftChar(1);
    mStatusChanges.setFlag(SynStatusChange::scAll);
}

void SynEdit::onLinesDeleted(int index, int count)
{
    if (mHighlighter && mDocument->count() > 0)
        scanFrom(index, index+1);
    if (mUseCodeFolding)
        foldOnListDeleted(index + 1, count);
    invalidateLines(index + 1, INT_MAX);
    invalidateGutterLines(index + 1, INT_MAX);
}

void SynEdit::onLinesInserted(int index, int count)
{
    if (mUseCodeFolding)
        foldOnListInserted(index + 1, count);
    if (mHighlighter && mDocument->count() > 0) {
//        int vLastScan = index;
//        do {
          scanFrom(index, index+count);
//            vLastScan++;
//        } while (vLastScan < index + count) ;
    }
    invalidateLines(index + 1, INT_MAX);
    invalidateGutterLines(index + 1, INT_MAX);
}

void SynEdit::onLinesPutted(int index, int count)
{
    int vEndLine = index + 1;
    if (mHighlighter) {
        vEndLine = std::max(vEndLine, scanFrom(index, index+count) + 1);
    }
    invalidateLines(index + 1, vEndLine);
}

void SynEdit::onUndoAdded()
{
    updateModifiedStatus();

    // we have to clear the redo information, since adding undo info removes
    // the necessary context to undo earlier edit actions
    if (! mUndoList->insideRedo() &&
            mUndoList->PeekItem() && (mUndoList->PeekItem()->changeReason()!=SynChangeReason::crGroupBreak))
        mRedoList->Clear();
    if (mUndoList->blockCount() == 0 )
        onChanged();
}

SynSelectionMode SynEdit::activeSelectionMode() const
{
    return mActiveSelectionMode;
}

void SynEdit::setActiveSelectionMode(const SynSelectionMode &Value)
{
    if (mActiveSelectionMode != Value) {
        if (selAvail())
            invalidateSelection();
        mActiveSelectionMode = Value;
        if (selAvail())
            invalidateSelection();
        setStatusChanged(SynStatusChange::scSelection);
    }
}

BufferCoord SynEdit::blockEnd() const
{
    if (mActiveSelectionMode==SynSelectionMode::smColumn)
        return mBlockEnd;
    if ((mBlockEnd.Line < mBlockBegin.Line)
      || ((mBlockEnd.Line == mBlockBegin.Line) && (mBlockEnd.Char < mBlockBegin.Char)))
        return mBlockBegin;
    else
        return mBlockEnd;
}

void SynEdit::setBlockEnd(BufferCoord Value)
{
    //setActiveSelectionMode(mSelectionMode);
    Value.Line = minMax(Value.Line, 1, mDocument->count());
    if (mActiveSelectionMode == SynSelectionMode::smNormal) {
      if (Value.Line >= 1 && Value.Line <= mDocument->count())
          Value.Char = std::min(Value.Char, getDisplayStringAtLine(Value.Line).length() + 1);
      else
          Value.Char = 1;
    } else {
        int maxLen = mDocument->lengthOfLongestLine();
        if (highlighter())
            maxLen = maxLen+stringColumns(highlighter()->foldString(),maxLen);
        Value.Char = minMax(Value.Char, 1, maxLen+1);
    }
    if (Value.Char != mBlockEnd.Char || Value.Line != mBlockEnd.Line) {
        if (mActiveSelectionMode == SynSelectionMode::smColumn && Value.Char != mBlockEnd.Char) {
            invalidateLines(
                        std::min(mBlockBegin.Line, std::min(mBlockEnd.Line, Value.Line)),
                        std::max(mBlockBegin.Line, std::max(mBlockEnd.Line, Value.Line)));
            mBlockEnd = Value;
        } else {
            int nLine = mBlockEnd.Line;
            mBlockEnd = Value;
            if (mActiveSelectionMode != SynSelectionMode::smColumn || mBlockBegin.Char != mBlockEnd.Char)
                invalidateLines(nLine, mBlockEnd.Line);
        }
        setStatusChanged(SynStatusChange::scSelection);
    }
}

void SynEdit::setSelLength(int Value)
{
    if (mBlockBegin.Line>mDocument->count() || mBlockBegin.Line<=0)
        return;

    if (Value >= 0) {
        int y = mBlockBegin.Line;
        int ch = mBlockBegin.Char;
        int x = ch + Value;
        QString line;
        while (y<=mDocument->count()) {
            line = mDocument->getString(y-1);
            if (x <= line.length()+2) {
                if (x==line.length()+2)
                    x = line.length()+1;
                break;
            }
            x -= line.length()+2;
            y ++;
        }
        if (y>mDocument->count()) {
            y = mDocument->count();
            x = mDocument->getString(y-1).length()+1;
        }
        BufferCoord iNewEnd{x,y};
        setCaretAndSelection(iNewEnd, mBlockBegin, iNewEnd);
    } else {
        int y = mBlockBegin.Line;
        int ch = mBlockBegin.Char;
        int x = ch + Value;
        QString line;
        while (y>=1) {
            if (x>=0) {
                if (x==0)
                    x = 1;
                break;
            }
            y--;
            line = mDocument->getString(y-1);
            x += line.length()+2;
        }
        if (y>mDocument->count()) {
            y = mDocument->count();
            x = mDocument->getString(y-1).length()+1;
        }
        BufferCoord iNewStart{x,y};
        setCaretAndSelection(iNewStart, iNewStart, mBlockBegin);
    }
}

void SynEdit::setSelText(const QString &text)
{
    doSetSelText(text);
}

BufferCoord SynEdit::blockBegin() const
{
    if (mActiveSelectionMode==SynSelectionMode::smColumn)
        return mBlockBegin;
    if ((mBlockEnd.Line < mBlockBegin.Line)
      || ((mBlockEnd.Line == mBlockBegin.Line) && (mBlockEnd.Char < mBlockBegin.Char)))
        return mBlockEnd;
    else
        return mBlockBegin;
}

void SynEdit::setBlockBegin(BufferCoord value)
{
    int nInval1, nInval2;
    bool SelChanged;
    //setActiveSelectionMode(mSelectionMode);
    value.Line = minMax(value.Line, 1, mDocument->count());
    if (mActiveSelectionMode == SynSelectionMode::smNormal) {
        if (value.Line >= 1 && value.Line <= mDocument->count())
            value.Char = std::min(value.Char, getDisplayStringAtLine(value.Line).length() + 1);
        else
            value.Char = 1;
    } else {
        int maxLen = mDocument->lengthOfLongestLine();
        if (highlighter())
            maxLen = maxLen+stringColumns(highlighter()->foldString(),maxLen);
        value.Char = minMax(value.Char, 1, maxLen+1);
    }
    if (selAvail()) {
        if (mBlockBegin.Line < mBlockEnd.Line) {
            nInval1 = std::min(value.Line, mBlockBegin.Line);
            nInval2 = std::max(value.Line, mBlockEnd.Line);
        } else {
            nInval1 = std::min(value.Line, mBlockEnd.Line);
            nInval2 = std::max(value.Line, mBlockBegin.Line);
        };
        mBlockBegin = value;
        mBlockEnd = value;
        invalidateLines(nInval1, nInval2);
        SelChanged = true;
    } else {
        SelChanged =
          (mBlockBegin.Char != value.Char) || (mBlockBegin.Line != value.Line) ||
          (mBlockEnd.Char != value.Char) || (mBlockEnd.Line != value.Line);
        mBlockBegin = value;
        mBlockEnd = value;
    }
    if (SelChanged)
        setStatusChanged(SynStatusChange::scSelection);
}

int SynEdit::leftChar() const
{
    return mLeftChar;
}

void SynEdit::setLeftChar(int Value)
{
    //int MaxVal;
    //QRect iTextArea;
    Value = std::min(Value,maxScrollWidth());
    if (Value != mLeftChar) {
        horizontalScrollBar()->setValue(Value);
        setStatusChanged(SynStatusChange::scLeftChar);
    }
}

int SynEdit::linesInWindow() const
{
    return mLinesInWindow;
}

int SynEdit::topLine() const
{
    return mTopLine;
}

void SynEdit::setTopLine(int Value)
{
    Value = std::min(Value,maxScrollHeight());
//    if (mOptions.testFlag(SynEditorOption::eoScrollPastEof))
//        Value = std::min(Value, displayLineCount());
//    else
//        Value = std::min(Value, displayLineCount() - mLinesInWindow + 1);
    Value = std::max(Value, 1);
    if (Value != mTopLine) {
        verticalScrollBar()->setValue(Value);
        setStatusChanged(SynStatusChange::scTopLine);
    }
}

void SynEdit::onRedoAdded()
{
    updateModifiedStatus();

    if (mRedoList->blockCount() == 0 )
        onChanged();
}

void SynEdit::onGutterChanged()
{
    if (mGutter.showLineNumbers() && mGutter.autoSize())
        mGutter.autoSizeDigitCount(mDocument->count());
    int nW;
    if (mGutter.useFontStyle()) {
        QFontMetrics fm=QFontMetrics(mGutter.font());
        nW = mGutter.realGutterWidth(fm.averageCharWidth());
    } else {
        nW = mGutter.realGutterWidth(mCharWidth);
    }
    if (nW == mGutterWidth)
        invalidateGutter();
    else
        setGutterWidth(nW);
}

void SynEdit::onScrollTimeout()
{
    doMouseScroll(false);
}

void SynEdit::onDraggingScrollTimeout()
{
    doMouseScroll(true);
}
