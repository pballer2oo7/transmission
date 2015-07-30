/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <cassert>

#include <QtGui>
#include <QCheckBox>
#include <QIcon>
#include <QProxyStyle>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "AboutDialog.h"
#include "AddData.h"
#include "Application.h"
#include "DetailsDialog.h"
#include "FilterBar.h"
#include "Filters.h"
#include "Formatter.h"
#include "MainWindow.h"
#include "MakeDialog.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "RelocateDialog.h"
#include "Session.h"
#include "SessionDialog.h"
#include "Speed.h"
#include "StatsDialog.h"
#include "TorrentDelegate.h"
#include "TorrentDelegateMin.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"


/**
 * This is a proxy-style for that forces it to be always disabled.
 * We use this to make our torrent list view behave consistently on
 * both GTK and Qt implementations.
 */
class ListViewProxyStyle: public QProxyStyle
{
  public:

    int styleHint (StyleHint            hint,
                   const QStyleOption * option = 0,
                   const QWidget      * widget = 0,
                   QStyleHintReturn   * returnData = 0) const
    {
      if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
        return 0;
      return QProxyStyle::styleHint (hint, option, widget, returnData);
    }
};


QIcon
MainWindow::getStockIcon (const QString& name, int fallback)
{
  QIcon icon = QIcon::fromTheme (name);

  if (icon.isNull () && (fallback >= 0))
    icon = style ()->standardIcon (QStyle::StandardPixmap (fallback), 0, this);

  return icon;
}

MainWindow::MainWindow (Session& session, Prefs& prefs, TorrentModel& model, bool minimized):
  mySession (session),
  myPrefs (prefs),
  myModel (model),
  myLastFullUpdateTime (0),
  mySessionDialog (),
  myPrefsDialog (),
  myAboutDialog (),
  myStatsDialog (),
  myDetailsDialog (),
  myFilterModel (prefs),
  myTorrentDelegate (new TorrentDelegate (this)),
  myTorrentDelegateMin (new TorrentDelegateMin (this)),
  myLastSendTime (0),
  myLastReadTime (0),
  myNetworkTimer (this),
  myNetworkError (false),
  myRefreshTrayIconTimer (this),
  myRefreshActionSensitivityTimer (this)
{
  setAcceptDrops (true);

  QAction * sep = new QAction (this);
  sep->setSeparator (true);

  ui.setupUi (this);

  QStyle * style = this->style ();

  int i = style->pixelMetric (QStyle::PM_SmallIconSize, 0, this);
  const QSize smallIconSize (i, i);

  ui.listView->setStyle (new ListViewProxyStyle);
  ui.listView->setAttribute (Qt::WA_MacShowFocusRect, false);

  // icons
  ui.action_OpenFile->setIcon (getStockIcon (QLatin1String ("document-open"), QStyle::SP_DialogOpenButton));
  ui.action_New->setIcon (getStockIcon (QLatin1String ("document-new"), QStyle::SP_DesktopIcon));
  ui.action_Properties->setIcon (getStockIcon (QLatin1String ("document-properties"), QStyle::SP_DesktopIcon));
  ui.action_OpenFolder->setIcon (getStockIcon (QLatin1String ("folder-open"), QStyle::SP_DirOpenIcon));
  ui.action_Start->setIcon (getStockIcon (QLatin1String ("media-playback-start"), QStyle::SP_MediaPlay));
  ui.action_StartNow->setIcon (getStockIcon (QLatin1String ("media-playback-start"), QStyle::SP_MediaPlay));
  ui.action_Announce->setIcon (getStockIcon (QLatin1String ("network-transmit-receive")));
  ui.action_Pause->setIcon (getStockIcon (QLatin1String ("media-playback-pause"), QStyle::SP_MediaPause));
  ui.action_Remove->setIcon (getStockIcon (QLatin1String ("list-remove"), QStyle::SP_TrashIcon));
  ui.action_Delete->setIcon (getStockIcon (QLatin1String ("edit-delete"), QStyle::SP_TrashIcon));
  ui.action_StartAll->setIcon (getStockIcon (QLatin1String ("media-playback-start"), QStyle::SP_MediaPlay));
  ui.action_PauseAll->setIcon (getStockIcon (QLatin1String ("media-playback-pause"), QStyle::SP_MediaPause));
  ui.action_Quit->setIcon (getStockIcon (QLatin1String ("application-exit")));
  ui.action_SelectAll->setIcon (getStockIcon (QLatin1String ("edit-select-all")));
  ui.action_ReverseSortOrder->setIcon (getStockIcon (QLatin1String ("view-sort-ascending"), QStyle::SP_ArrowDown));
  ui.action_Preferences->setIcon (getStockIcon (QLatin1String ("preferences-system")));
  ui.action_Contents->setIcon (getStockIcon (QLatin1String ("help-contents"), QStyle::SP_DialogHelpButton));
  ui.action_About->setIcon (getStockIcon (QLatin1String ("help-about")));
  ui.action_QueueMoveTop->setIcon (getStockIcon (QLatin1String ("go-top")));
  ui.action_QueueMoveUp->setIcon (getStockIcon (QLatin1String ("go-up"), QStyle::SP_ArrowUp));
  ui.action_QueueMoveDown->setIcon (getStockIcon (QLatin1String ("go-down"), QStyle::SP_ArrowDown));
  ui.action_QueueMoveBottom->setIcon (getStockIcon (QLatin1String ("go-bottom")));

  // ui signals
  connect (ui.action_Toolbar, SIGNAL (toggled (bool)), this, SLOT (setToolbarVisible (bool)));
  connect (ui.action_Filterbar, SIGNAL (toggled (bool)), this, SLOT (setFilterbarVisible (bool)));
  connect (ui.action_Statusbar, SIGNAL (toggled (bool)), this, SLOT (setStatusbarVisible (bool)));
  connect (ui.action_CompactView, SIGNAL (toggled (bool)), this, SLOT (setCompactView (bool)));
  connect (ui.action_SortByActivity, SIGNAL (toggled (bool)), this, SLOT (onSortByActivityToggled (bool)));
  connect (ui.action_SortByAge,      SIGNAL (toggled (bool)), this, SLOT (onSortByAgeToggled (bool)));
  connect (ui.action_SortByETA,      SIGNAL (toggled (bool)), this, SLOT (onSortByETAToggled (bool)));
  connect (ui.action_SortByName,     SIGNAL (toggled (bool)), this, SLOT (onSortByNameToggled (bool)));
  connect (ui.action_SortByProgress, SIGNAL (toggled (bool)), this, SLOT (onSortByProgressToggled (bool)));
  connect (ui.action_SortByQueue,    SIGNAL (toggled (bool)), this, SLOT (onSortByQueueToggled (bool)));
  connect (ui.action_SortByRatio,    SIGNAL (toggled (bool)), this, SLOT (onSortByRatioToggled (bool)));
  connect (ui.action_SortBySize,     SIGNAL (toggled (bool)), this, SLOT (onSortBySizeToggled (bool)));
  connect (ui.action_SortByState,    SIGNAL (toggled (bool)), this, SLOT (onSortByStateToggled (bool)));
  connect (ui.action_ReverseSortOrder, SIGNAL (toggled (bool)), this, SLOT (setSortAscendingPref (bool)));
  connect (ui.action_Start, SIGNAL (triggered ()), this, SLOT (startSelected ()));
  connect (ui.action_QueueMoveTop,    SIGNAL (triggered ()), this, SLOT (queueMoveTop ()));
  connect (ui.action_QueueMoveUp,     SIGNAL (triggered ()), this, SLOT (queueMoveUp ()));
  connect (ui.action_QueueMoveDown,   SIGNAL (triggered ()), this, SLOT (queueMoveDown ()));
  connect (ui.action_QueueMoveBottom, SIGNAL (triggered ()), this, SLOT (queueMoveBottom ()));
  connect (ui.action_StartNow, SIGNAL (triggered ()), this, SLOT (startSelectedNow ()));
  connect (ui.action_Pause, SIGNAL (triggered ()), this, SLOT (pauseSelected ()));
  connect (ui.action_Remove, SIGNAL (triggered ()), this, SLOT (removeSelected ()));
  connect (ui.action_Delete, SIGNAL (triggered ()), this, SLOT (deleteSelected ()));
  connect (ui.action_Verify, SIGNAL (triggered ()), this, SLOT (verifySelected ()));
  connect (ui.action_Announce, SIGNAL (triggered ()), this, SLOT (reannounceSelected ()));
  connect (ui.action_StartAll, SIGNAL (triggered ()), this, SLOT (startAll ()));
  connect (ui.action_PauseAll, SIGNAL (triggered ()), this, SLOT (pauseAll ()));
  connect (ui.action_OpenFile, SIGNAL (triggered ()), this, SLOT (openTorrent ()));
  connect (ui.action_AddURL, SIGNAL (triggered ()), this, SLOT (openURL ()));
  connect (ui.action_New, SIGNAL (triggered ()), this, SLOT (newTorrent ()));
  connect (ui.action_Preferences, SIGNAL (triggered ()), this, SLOT (openPreferences ()));
  connect (ui.action_Statistics, SIGNAL (triggered ()), this, SLOT (openStats ()));
  connect (ui.action_Donate, SIGNAL (triggered ()), this, SLOT (openDonate ()));
  connect (ui.action_About, SIGNAL (triggered ()), this, SLOT (openAbout ()));
  connect (ui.action_Contents, SIGNAL (triggered ()), this, SLOT (openHelp ()));
  connect (ui.action_OpenFolder, SIGNAL (triggered ()), this, SLOT (openFolder ()));
  connect (ui.action_CopyMagnetToClipboard, SIGNAL (triggered ()), this, SLOT (copyMagnetLinkToClipboard ()));
  connect (ui.action_SetLocation, SIGNAL (triggered ()), this, SLOT (setLocation ()));
  connect (ui.action_Properties, SIGNAL (triggered ()), this, SLOT (openProperties ()));
  connect (ui.action_SessionDialog, SIGNAL (triggered ()), this, SLOT (openSession ()));

  connect (ui.listView, SIGNAL (activated (QModelIndex)), ui.action_Properties, SLOT (trigger ()));

  // signals
  connect (ui.action_SelectAll, SIGNAL (triggered ()), ui.listView, SLOT (selectAll ()));
  connect (ui.action_DeselectAll, SIGNAL (triggered ()), ui.listView, SLOT (clearSelection ()));

  connect (&myFilterModel, SIGNAL (rowsInserted (QModelIndex, int, int)), this, SLOT (refreshActionSensitivitySoon ()));
  connect (&myFilterModel, SIGNAL (rowsRemoved (QModelIndex, int, int)), this, SLOT (refreshActionSensitivitySoon ()));

  connect (ui.action_Quit, SIGNAL (triggered ()), qApp, SLOT (quit ()));

  // torrent view
  myFilterModel.setSourceModel (&myModel);
  connect (&myModel, SIGNAL (modelReset ()), this, SLOT (onModelReset ()));
  connect (&myModel, SIGNAL (rowsRemoved (QModelIndex, int, int)), this, SLOT (onModelReset ()));
  connect (&myModel, SIGNAL (rowsInserted (QModelIndex, int, int)), this, SLOT (onModelReset ()));
  connect (&myModel, SIGNAL (dataChanged (QModelIndex, QModelIndex)), this, SLOT (refreshTrayIconSoon ()));

  ui.listView->setModel (&myFilterModel);
  connect (ui.listView->selectionModel (), SIGNAL (selectionChanged (QItemSelection, QItemSelection)), this, SLOT (refreshActionSensitivitySoon ()));

  QActionGroup * actionGroup = new QActionGroup (this);
  actionGroup->addAction (ui.action_SortByActivity);
  actionGroup->addAction (ui.action_SortByAge);
  actionGroup->addAction (ui.action_SortByETA);
  actionGroup->addAction (ui.action_SortByName);
  actionGroup->addAction (ui.action_SortByProgress);
  actionGroup->addAction (ui.action_SortByQueue);
  actionGroup->addAction (ui.action_SortByRatio);
  actionGroup->addAction (ui.action_SortBySize);
  actionGroup->addAction (ui.action_SortByState);

  myAltSpeedAction = new QAction (tr ("Speed Limits"), this);
  myAltSpeedAction->setIcon (ui.altSpeedButton->icon ());
  myAltSpeedAction->setCheckable (true);
  connect (myAltSpeedAction, SIGNAL (triggered ()), this, SLOT (toggleSpeedMode ()));

  QMenu * menu = new QMenu (this);
  menu->addAction (ui.action_OpenFile);
  menu->addAction (ui.action_AddURL);
  menu->addSeparator ();
  menu->addAction (ui.action_ShowMainWindow);
  menu->addAction (ui.action_ShowMessageLog);
  menu->addAction (ui.action_About);
  menu->addSeparator ();
  menu->addAction (ui.action_StartAll);
  menu->addAction (ui.action_PauseAll);
  menu->addAction (myAltSpeedAction);
  menu->addSeparator ();
  menu->addAction (ui.action_Quit);
  myTrayIcon.setContextMenu (menu);
  myTrayIcon.setIcon (QIcon::fromTheme (QLatin1String ("transmission-tray-icon"), qApp->windowIcon ()));

  connect (&myPrefs, SIGNAL (changed (int)), this, SLOT (refreshPref (int)));
  connect (ui.action_ShowMainWindow, SIGNAL (triggered (bool)), this, SLOT (toggleWindows (bool)));
  connect (&myTrayIcon, SIGNAL (activated (QSystemTrayIcon::ActivationReason)),
           this, SLOT (trayActivated (QSystemTrayIcon::ActivationReason)));

  toggleWindows (!minimized);
  ui.action_TrayIcon->setChecked (minimized || prefs.getBool (Prefs::SHOW_TRAY_ICON));

  initStatusBar ();
  ui.verticalLayout->insertWidget (0, myFilterBar = new FilterBar (myPrefs, myModel, myFilterModel));

  QList<int> initKeys;
  initKeys << Prefs::MAIN_WINDOW_X
           << Prefs::SHOW_TRAY_ICON
           << Prefs::SORT_REVERSED
           << Prefs::SORT_MODE
           << Prefs::FILTERBAR
           << Prefs::STATUSBAR
           << Prefs::STATUSBAR_STATS
           << Prefs::TOOLBAR
           << Prefs::ALT_SPEED_LIMIT_ENABLED
           << Prefs::COMPACT_VIEW
           << Prefs::DSPEED
           << Prefs::DSPEED_ENABLED
           << Prefs::USPEED
           << Prefs::USPEED_ENABLED
           << Prefs::RATIO
           << Prefs::RATIO_ENABLED;
  for (const int key: initKeys)
    refreshPref (key);

  connect (&mySession, SIGNAL (sourceChanged ()), this, SLOT (onSessionSourceChanged ()));
  connect (&mySession, SIGNAL (statsUpdated ()), this, SLOT (refreshStatusBar ()));
  connect (&mySession, SIGNAL (dataReadProgress ()), this, SLOT (dataReadProgress ()));
  connect (&mySession, SIGNAL (dataSendProgress ()), this, SLOT (dataSendProgress ()));
  connect (&mySession, SIGNAL (httpAuthenticationRequired ()), this, SLOT (wrongAuthentication ()));
  connect (&mySession, SIGNAL (error (QNetworkReply::NetworkError)), this, SLOT (onError (QNetworkReply::NetworkError)));
  connect (&mySession, SIGNAL (errorMessage (QString)), this, SLOT (errorMessage(QString)));

  if (mySession.isServer ())
    {
      ui.networkLabel->hide ();
    }
  else
    {
      connect (&myNetworkTimer, SIGNAL (timeout ()), this, SLOT (onNetworkTimer ()));
      myNetworkTimer.start (1000);
    }

  connect (&myRefreshTrayIconTimer, SIGNAL (timeout ()), this, SLOT (refreshTrayIcon ()));
  connect (&myRefreshActionSensitivityTimer, SIGNAL (timeout ()), this, SLOT (refreshActionSensitivity ()));


  refreshActionSensitivitySoon ();
  refreshTrayIconSoon ();
  refreshStatusBar ();
  refreshTitle ();
}

MainWindow::~MainWindow ()
{
}

/****
*****
****/

void
MainWindow::onSessionSourceChanged ()
{
  myModel.clear ();
}

void
MainWindow::onModelReset ()
{
  refreshTitle ();
  refreshActionSensitivitySoon ();
  refreshStatusBar ();
  refreshTrayIconSoon ();
}

/****
*****
****/

#define PREF_VARIANTS_KEY "pref-variants-list"

void
MainWindow::onSetPrefs ()
{
  const QVariantList p = sender ()->property (PREF_VARIANTS_KEY).toList ();
  assert ( (p.size () % 2) == 0);
  for (int i=0, n=p.size (); i<n; i+=2)
    myPrefs.set (p[i].toInt (), p[i+1]);
}

void
MainWindow::onSetPrefs (bool isChecked)
{
  if (isChecked)
    onSetPrefs ();
}

void
MainWindow::initStatusBar ()
{
  ui.optionsButton->setMenu (createOptionsMenu ());

  const int minimumSpeedWidth = ui.downloadSpeedLabel->fontMetrics ().width (Formatter::uploadSpeedToString (Speed::fromKBps (999.99)));
  ui.downloadSpeedLabel->setMinimumWidth (minimumSpeedWidth);
  ui.uploadSpeedLabel->setMinimumWidth (minimumSpeedWidth);

  ui.statsModeButton->setMenu (createStatsModeMenu ());

  connect (ui.altSpeedButton, SIGNAL (clicked ()), this, SLOT (toggleSpeedMode ()));
}

QMenu *
MainWindow::createOptionsMenu ()
{
  QMenu * menu;
  QMenu * sub;
  QAction * a;
  QActionGroup * g;

  QList<int> stockSpeeds;
  stockSpeeds << 5 << 10 << 20 << 30 << 40 << 50 << 75 << 100 << 150 << 200 << 250 << 500 << 750;
  QList<double> stockRatios;
  stockRatios << 0.25 << 0.50 << 0.75 << 1 << 1.5 << 2 << 3;

  menu = new QMenu (this);
  sub = menu->addMenu (tr ("Limit Download Speed"));

    int currentVal = myPrefs.get<int> (Prefs::DSPEED);
    g = new QActionGroup (this);
    a = myDlimitOffAction = sub->addAction (tr ("Unlimited"));
    a->setCheckable (true);
    a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::DSPEED_ENABLED << false);
    g->addAction (a);
    connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs (bool)));
    a = myDlimitOnAction = sub->addAction (tr ("Limited at %1").arg (Formatter::speedToString (Speed::fromKBps (currentVal))));
    a->setCheckable (true);
    a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::DSPEED << currentVal << Prefs::DSPEED_ENABLED << true);
    g->addAction (a);
    connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs (bool)));
    sub->addSeparator ();
    for (const int i: stockSpeeds)
      {
        a = sub->addAction (Formatter::speedToString (Speed::fromKBps (i)));
        a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::DSPEED << i << Prefs::DSPEED_ENABLED << true);
        connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs ()));
      }

  sub = menu->addMenu (tr ("Limit Upload Speed"));

    currentVal = myPrefs.get<int> (Prefs::USPEED);
    g = new QActionGroup (this);
    a = myUlimitOffAction = sub->addAction (tr ("Unlimited"));
    a->setCheckable (true);
    a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::USPEED_ENABLED << false);
    g->addAction (a);
    connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs (bool)));
    a = myUlimitOnAction = sub->addAction (tr ("Limited at %1").arg (Formatter::speedToString (Speed::fromKBps (currentVal))));
    a->setCheckable (true);
    a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::USPEED << currentVal << Prefs::USPEED_ENABLED << true);
    g->addAction (a);
    connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs (bool)));
    sub->addSeparator ();
    for (const int i: stockSpeeds)
      {
        a = sub->addAction (Formatter::speedToString (Speed::fromKBps (i)));
        a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::USPEED << i << Prefs::USPEED_ENABLED << true);
        connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs ()));
      }

  menu->addSeparator ();
  sub = menu->addMenu (tr ("Stop Seeding at Ratio"));

    double d = myPrefs.get<double> (Prefs::RATIO);
    g = new QActionGroup (this);
    a = myRatioOffAction = sub->addAction (tr ("Seed Forever"));
    a->setCheckable (true);
    a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::RATIO_ENABLED << false);
    g->addAction (a);
    connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs (bool)));
    a = myRatioOnAction = sub->addAction (tr ("Stop at Ratio (%1)").arg (Formatter::ratioToString (d)));
    a->setCheckable (true);
    a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::RATIO << d << Prefs::RATIO_ENABLED << true);
    g->addAction (a);
    connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs (bool)));
    sub->addSeparator ();
    for (const double i: stockRatios)
      {
        a = sub->addAction (Formatter::ratioToString (i));
        a->setProperty (PREF_VARIANTS_KEY, QVariantList () << Prefs::RATIO << i << Prefs::RATIO_ENABLED << true);
        connect (a, SIGNAL (triggered (bool)), this, SLOT (onSetPrefs ()));
      }

  return menu;
}

QMenu *
MainWindow::createStatsModeMenu ()
{
  QActionGroup * a = new QActionGroup (this);
  a->addAction (ui.action_TotalRatio);
  a->addAction (ui.action_TotalTransfer);
  a->addAction (ui.action_SessionRatio);
  a->addAction (ui.action_SessionTransfer);

  QMenu * m = new QMenu (this);
  m->addAction (ui.action_TotalRatio);
  m->addAction (ui.action_TotalTransfer);
  m->addAction (ui.action_SessionRatio);
  m->addAction (ui.action_SessionTransfer);

  connect (ui.action_TotalRatio, SIGNAL (triggered ()), this, SLOT (showTotalRatio ()));
  connect (ui.action_TotalTransfer, SIGNAL (triggered ()), this, SLOT (showTotalTransfer ()));
  connect (ui.action_SessionRatio, SIGNAL (triggered ()), this, SLOT (showSessionRatio ()));
  connect (ui.action_SessionTransfer, SIGNAL (triggered ()), this, SLOT (showSessionTransfer ()));

  return m;
}

/****
*****
****/

void
MainWindow::setSortPref (int i)
{
  myPrefs.set (Prefs::SORT_MODE, SortMode (i));
}
void MainWindow::onSortByActivityToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_ACTIVITY); }
void MainWindow::onSortByAgeToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_AGE); }
void MainWindow::onSortByETAToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_ETA); }
void MainWindow::onSortByNameToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_NAME); }
void MainWindow::onSortByProgressToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_PROGRESS); }
void MainWindow::onSortByQueueToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_QUEUE); }
void MainWindow::onSortByRatioToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_RATIO); }
void MainWindow::onSortBySizeToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_SIZE); }
void MainWindow::onSortByStateToggled (bool b) { if (b) setSortPref (SortMode::SORT_BY_STATE); }

void
MainWindow::setSortAscendingPref (bool b)
{
  myPrefs.set (Prefs::SORT_REVERSED, b);
}

/****
*****
****/

void
MainWindow::showEvent (QShowEvent * event)
{
  Q_UNUSED (event);

  ui.action_ShowMainWindow->setChecked (true);
}

/****
*****
****/

void
MainWindow::hideEvent (QHideEvent * event)
{
  Q_UNUSED (event);

  if (!isVisible ())
    ui.action_ShowMainWindow->setChecked (false);
}

/****
*****
****/

void
MainWindow::openSession ()
{
  Utils::openDialog (mySessionDialog, mySession, myPrefs, this);
}

void
MainWindow::openPreferences ()
{
  Utils::openDialog (myPrefsDialog, mySession, myPrefs, this);
}

void
MainWindow::openProperties ()
{
  Utils::openDialog (myDetailsDialog, mySession, myPrefs, myModel, this);
  myDetailsDialog->setIds (getSelectedTorrents ());
}

void
MainWindow::setLocation ()
{
  QDialog * d = new RelocateDialog (mySession, myModel, getSelectedTorrents (), this);
  d->setAttribute (Qt::WA_DeleteOnClose, true);
  d->show ();
}

// Open Folder & select torrent's file or top folder
#undef HAVE_OPEN_SELECT
#if defined (Q_OS_WIN)
# define HAVE_OPEN_SELECT
static
void openSelect (const QString& path)
{
  const QString explorer = QLatin1String ("explorer");
  QString param;
  if (!QFileInfo (path).isDir ())
    param = QLatin1String ("/select,");
  param += QDir::toNativeSeparators (path);
  QProcess::startDetached (explorer, QStringList (param));
}
#elif defined (Q_OS_MAC)
# define HAVE_OPEN_SELECT
static
void openSelect (const QString& path)
{
  QStringList scriptArgs;
  scriptArgs << QLatin1String ("-e")
             << QString::fromLatin1 ("tell application \"Finder\" to reveal POSIX file \"%1\"").arg (path);
  QProcess::execute (QLatin1String ("/usr/bin/osascript"), scriptArgs);

  scriptArgs.clear ();
  scriptArgs << QLatin1String ("-e")
             << QLatin1String ("tell application \"Finder\" to activate");
  QProcess::execute (QLatin1String ("/usr/bin/osascript"), scriptArgs);
}
#endif

void
MainWindow::openFolder ()
{
  const int torrentId (*getSelectedTorrents ().begin ());
  const Torrent * tor (myModel.getTorrentFromId (torrentId));
  QString path (tor->getPath ());
  const FileList files = tor->files ();
  const QString firstfile = files.at (0).filename;
  int slashIndex = firstfile.indexOf (QLatin1Char ('/'));
  if (slashIndex > -1)
    {
      path = path + QLatin1Char ('/') + firstfile.left (slashIndex);
    }
#ifdef HAVE_OPEN_SELECT
  else
    {
      openSelect (path + QLatin1Char ('/') + firstfile);
      return;
    }
#endif
  QDesktopServices::openUrl (QUrl::fromLocalFile (path));
}

void
MainWindow::copyMagnetLinkToClipboard ()
{
  const int id (*getSelectedTorrents ().begin ());
  mySession.copyMagnetLinkToClipboard (id);
}

void
MainWindow::openStats ()
{
  Utils::openDialog (myStatsDialog, mySession, this);
}

void
MainWindow::openDonate ()
{
  QDesktopServices::openUrl (QUrl (QLatin1String ("http://www.transmissionbt.com/donate.php")));
}

void
MainWindow::openAbout ()
{
  Utils::openDialog (myAboutDialog, this);
}

void
MainWindow::openHelp ()
{
  QDesktopServices::openUrl (QUrl (QString::fromLatin1 ("http://www.transmissionbt.com/help/gtk/%1.%2x").
    arg (MAJOR_VERSION).arg (MINOR_VERSION / 10)));
}

void
MainWindow::refreshTitle ()
{
  QString title (QLatin1String ("Transmission"));
  const QUrl url (mySession.getRemoteUrl ());
  if (!url.isEmpty ())
    //: Second (optional) part of main window title "Transmission - host:port" (added when connected to remote session);
    //: notice that leading space (before the dash) is included here
    title += tr (" - %1:%2").arg (url.host ()).arg (url.port ());
  setWindowTitle (title);
}

void
MainWindow::refreshTrayIconSoon ()
{
  if (!myRefreshTrayIconTimer.isActive ())
    {
      myRefreshTrayIconTimer.setSingleShot (true);
      myRefreshTrayIconTimer.start (100);
    }
}
void
MainWindow::refreshTrayIcon ()
{
  Speed upSpeed, downSpeed;
  size_t upCount, downCount;
  QString tip;

  myModel.getTransferSpeed (upSpeed, upCount, downSpeed, downCount);

  if (myNetworkError)
    {
      tip  = tr ("Network Error");
    }
  else if (!upCount && !downCount)
    {
      tip = tr ("Idle");
    }
  else if (downCount)
    {
      tip = Formatter::downloadSpeedToString(downSpeed) +
            QLatin1String ("   ") +
            Formatter::uploadSpeedToString(upSpeed);
    }
  else if (upCount)
    {
      tip = Formatter::uploadSpeedToString(upSpeed);
    }

  myTrayIcon.setToolTip (tip);
}

void
MainWindow::refreshStatusBar ()
{
  Speed upSpeed, downSpeed;
  size_t upCount, downCount;
  myModel.getTransferSpeed (upSpeed, upCount, downSpeed, downCount);

  ui.uploadSpeedLabel->setText (Formatter::uploadSpeedToString (upSpeed));
  ui.uploadSpeedLabel->setVisible (downCount || upCount);
  ui.downloadSpeedLabel->setText (Formatter::downloadSpeedToString (downSpeed));
  ui.downloadSpeedLabel->setVisible (downCount);

  ui.networkLabel->setVisible (!mySession.isServer ());

  const QString mode (myPrefs.getString (Prefs::STATUSBAR_STATS));
  QString str;

  if (mode == QLatin1String ("session-ratio"))
    {
      str = tr ("Ratio: %1").arg (Formatter::ratioToString (mySession.getStats ().ratio));
    }
  else if (mode == QLatin1String ("session-transfer"))
    {
      const tr_session_stats& stats (mySession.getStats ());
      str = tr ("Down: %1, Up: %2").arg (Formatter::sizeToString (stats.downloadedBytes))
                                      .arg (Formatter::sizeToString (stats.uploadedBytes));
    }
  else if (mode == QLatin1String ("total-transfer"))
    {
      const tr_session_stats& stats (mySession.getCumulativeStats ());
      str = tr ("Down: %1, Up: %2").arg (Formatter::sizeToString (stats.downloadedBytes))
                                   .arg (Formatter::sizeToString (stats.uploadedBytes));
    }
  else // default is "total-ratio"
    {
      str = tr ("Ratio: %1").arg (Formatter::ratioToString (mySession.getCumulativeStats ().ratio));
    }

  ui.statsLabel->setText (str);
}



void
MainWindow::refreshActionSensitivitySoon ()
{
  if (!myRefreshActionSensitivityTimer.isActive ())
    {
      myRefreshActionSensitivityTimer.setSingleShot (true);
      myRefreshActionSensitivityTimer.start (100);
    }
}
void
MainWindow::refreshActionSensitivity ()
{
  int selected (0);
  int paused (0);
  int queued (0);
  int selectedAndPaused (0);
  int selectedAndQueued (0);
  int canAnnounce (0);
  const QAbstractItemModel * model (ui.listView->model ());
  const QItemSelectionModel * selectionModel (ui.listView->selectionModel ());
  const int rowCount (model->rowCount ());

  // count how many torrents are selected, paused, etc
  for (int row=0; row<rowCount; ++row)
    {
      const QModelIndex modelIndex (model->index (row, 0));
      assert (model == modelIndex.model ());
      const Torrent * tor (model->data (modelIndex, TorrentModel::TorrentRole).value<const Torrent*> ());
      if (tor)
        {
          const bool isSelected (selectionModel->isSelected (modelIndex));
          const bool isPaused (tor->isPaused ());
          const bool isQueued (tor->isQueued ());
          if (isSelected) ++selected;
          if (isQueued) ++queued;
          if (isPaused) ++ paused;
          if (isSelected && isPaused) ++selectedAndPaused;
          if (isSelected && isQueued) ++selectedAndQueued;
          if (tor->canManualAnnounce ()) ++canAnnounce;
        }
    }

  const bool haveSelection (selected > 0);
  ui.action_Verify->setEnabled (haveSelection);
  ui.action_Remove->setEnabled (haveSelection);
  ui.action_Delete->setEnabled (haveSelection);
  ui.action_Properties->setEnabled (haveSelection);
  ui.action_DeselectAll->setEnabled (haveSelection);
  ui.action_SetLocation->setEnabled (haveSelection);

  const bool oneSelection (selected == 1);
  ui.action_OpenFolder->setEnabled (oneSelection && mySession.isLocal ());
  ui.action_CopyMagnetToClipboard->setEnabled (oneSelection);

  ui.action_SelectAll->setEnabled (selected < rowCount);
  ui.action_StartAll->setEnabled (paused > 0);
  ui.action_PauseAll->setEnabled (paused < rowCount);
  ui.action_Start->setEnabled (selectedAndPaused > 0);
  ui.action_StartNow->setEnabled (selectedAndPaused + selectedAndQueued > 0);
  ui.action_Pause->setEnabled (selectedAndPaused < selected);
  ui.action_Announce->setEnabled (selected > 0 && (canAnnounce == selected));

  ui.action_QueueMoveTop->setEnabled (haveSelection);
  ui.action_QueueMoveUp->setEnabled (haveSelection);
  ui.action_QueueMoveDown->setEnabled (haveSelection);
  ui.action_QueueMoveBottom->setEnabled (haveSelection);

  if (!myDetailsDialog.isNull ())
    myDetailsDialog->setIds (getSelectedTorrents ());
}

/**
***
**/

void
MainWindow::clearSelection ()
{
  ui.action_DeselectAll->trigger ();
}

QSet<int>
MainWindow::getSelectedTorrents () const
{
  QSet<int> ids;

  for (const QModelIndex& index: ui.listView->selectionModel ()->selectedRows ())
    {
      const Torrent * tor (index.data (TorrentModel::TorrentRole).value<const Torrent*> ());
      ids.insert (tor->id ());
    }

  return ids;
}

void
MainWindow::startSelected ()
{
  mySession.startTorrents (getSelectedTorrents ());
}
void
MainWindow::startSelectedNow ()
{
  mySession.startTorrentsNow (getSelectedTorrents ());
}
void
MainWindow::pauseSelected ()
{
  mySession.pauseTorrents (getSelectedTorrents ());
}
void
MainWindow::queueMoveTop ()
{
  mySession.queueMoveTop (getSelectedTorrents ());
}
void
MainWindow::queueMoveUp ()
{
  mySession.queueMoveUp (getSelectedTorrents ());
}
void
MainWindow::queueMoveDown ()
{
  mySession.queueMoveDown (getSelectedTorrents ());
}
void
MainWindow::queueMoveBottom ()
{
  mySession.queueMoveBottom (getSelectedTorrents ());
}
void
MainWindow::startAll ()
{
  mySession.startTorrents ();
}
void
MainWindow::pauseAll ()
{
  mySession.pauseTorrents ();
}
void
MainWindow::removeSelected ()
{
  removeTorrents (false);
}
void
MainWindow::deleteSelected ()
{
  removeTorrents (true);
}
void
MainWindow::verifySelected ()
{
  mySession.verifyTorrents (getSelectedTorrents ());
}
void
MainWindow::reannounceSelected ()
{
  mySession.reannounceTorrents (getSelectedTorrents ());
}

/**
***
**/

void MainWindow::showTotalRatio () { myPrefs.set (Prefs::STATUSBAR_STATS, QString::fromLatin1 ("total-ratio")); }
void MainWindow::showTotalTransfer () { myPrefs.set (Prefs::STATUSBAR_STATS, QString::fromLatin1 ("total-transfer")); }
void MainWindow::showSessionRatio () { myPrefs.set (Prefs::STATUSBAR_STATS, QString::fromLatin1 ("session-ratio")); }
void MainWindow::showSessionTransfer () { myPrefs.set (Prefs::STATUSBAR_STATS, QString::fromLatin1 ("session-transfer")); }

/**
***
**/

void
MainWindow::setCompactView (bool visible)
{
  myPrefs.set (Prefs::COMPACT_VIEW, visible);
}
void
MainWindow::toggleSpeedMode ()
{
  myPrefs.toggleBool (Prefs::ALT_SPEED_LIMIT_ENABLED);
  const bool mode = myPrefs.get<bool> (Prefs::ALT_SPEED_LIMIT_ENABLED);
  myAltSpeedAction->setChecked (mode);
}
void
MainWindow::setToolbarVisible (bool visible)
{
  myPrefs.set (Prefs::TOOLBAR, visible);
}
void
MainWindow::setFilterbarVisible (bool visible)
{
  myPrefs.set (Prefs::FILTERBAR, visible);
}
void
MainWindow::setStatusbarVisible (bool visible)
{
  myPrefs.set (Prefs::STATUSBAR, visible);
}

/**
***
**/

void
MainWindow::toggleWindows (bool doShow)
{
  if (!doShow)
    {
      hide ();
    }
  else
    {
      if (!isVisible ()) show ();
      if (isMinimized ()) showNormal ();
      //activateWindow ();
      raise ();
      qApp->setActiveWindow (this);
    }
}

void
MainWindow::trayActivated (QSystemTrayIcon::ActivationReason reason)
{
  if ((reason == QSystemTrayIcon::Trigger) ||
      (reason == QSystemTrayIcon::DoubleClick))
    {
      if (isMinimized ())
        toggleWindows (true);
      else
        toggleWindows (!isVisible ());
    }
}


void
MainWindow::refreshPref (int key)
{
  bool b;
  int i;
  QString str;

  switch (key)
    {
      case Prefs::STATUSBAR_STATS:
        str = myPrefs.getString (key);
        ui.action_TotalRatio->setChecked (str == QLatin1String ("total-ratio"));
        ui.action_TotalTransfer->setChecked (str == QLatin1String ("total-transfer"));
        ui.action_SessionRatio->setChecked (str == QLatin1String ("session-ratio"));
        ui.action_SessionTransfer->setChecked (str == QLatin1String ("session-transfer"));
        refreshStatusBar ();
        break;

      case Prefs::SORT_REVERSED:
        ui.action_ReverseSortOrder->setChecked (myPrefs.getBool (key));
        break;

      case Prefs::SORT_MODE:
        i = myPrefs.get<SortMode> (key).mode ();
        ui.action_SortByActivity->setChecked (i == SortMode::SORT_BY_ACTIVITY);
        ui.action_SortByAge->setChecked (i == SortMode::SORT_BY_AGE);
        ui.action_SortByETA->setChecked (i == SortMode::SORT_BY_ETA);
        ui.action_SortByName->setChecked (i == SortMode::SORT_BY_NAME);
        ui.action_SortByProgress->setChecked (i == SortMode::SORT_BY_PROGRESS);
        ui.action_SortByQueue->setChecked (i == SortMode::SORT_BY_QUEUE);
        ui.action_SortByRatio->setChecked (i == SortMode::SORT_BY_RATIO);
        ui.action_SortBySize->setChecked (i == SortMode::SORT_BY_SIZE);
        ui.action_SortByState->setChecked (i == SortMode::SORT_BY_STATE);
        break;

      case Prefs::DSPEED_ENABLED:
        (myPrefs.get<bool> (key) ? myDlimitOnAction : myDlimitOffAction)->setChecked (true);
        break;

      case Prefs::DSPEED:
        myDlimitOnAction->setText (tr ("Limited at %1").arg (Formatter::speedToString (Speed::fromKBps (myPrefs.get<int> (key)))));
        break;

      case Prefs::USPEED_ENABLED:
        (myPrefs.get<bool> (key) ? myUlimitOnAction : myUlimitOffAction)->setChecked (true);
        break;

      case Prefs::USPEED:
        myUlimitOnAction->setText (tr ("Limited at %1").arg (Formatter::speedToString (Speed::fromKBps (myPrefs.get<int> (key)))));
        break;

      case Prefs::RATIO_ENABLED:
        (myPrefs.get<bool> (key) ? myRatioOnAction : myRatioOffAction)->setChecked (true);
        break;

      case Prefs::RATIO:
        myRatioOnAction->setText (tr ("Stop at Ratio (%1)").arg (Formatter::ratioToString (myPrefs.get<double> (key))));
        break;

      case Prefs::FILTERBAR:
        b = myPrefs.getBool (key);
        myFilterBar->setVisible (b);
        ui.action_Filterbar->setChecked (b);
        break;

      case Prefs::STATUSBAR:
        b = myPrefs.getBool (key);
        ui.statusBar->setVisible (b);
        ui.action_Statusbar->setChecked (b);
        break;

      case Prefs::TOOLBAR:
        b = myPrefs.getBool (key);
        ui.toolBar->setVisible (b);
        ui.action_Toolbar->setChecked (b);
        break;

      case Prefs::SHOW_TRAY_ICON:
        b = myPrefs.getBool (key);
        ui.action_TrayIcon->setChecked (b);
        myTrayIcon.setVisible (b);
        qApp->setQuitOnLastWindowClosed (!b);
        refreshTrayIconSoon ();
        break;

      case Prefs::COMPACT_VIEW: {
#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0) // QTBUG-33537
            QItemSelectionModel * selectionModel (ui.listView->selectionModel ());
            const QItemSelection selection (selectionModel->selection ());
            const QModelIndex currentIndex (selectionModel->currentIndex ());
#endif
            b = myPrefs.getBool (key);
            ui.action_CompactView->setChecked (b);
            ui.listView->setItemDelegate (b ? myTorrentDelegateMin : myTorrentDelegate);
#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0) // QTBUG-33537
            selectionModel->clear ();
            ui.listView->reset (); // force the rows to resize
            selectionModel->select (selection, QItemSelectionModel::Select);
            selectionModel->setCurrentIndex (currentIndex, QItemSelectionModel::NoUpdate);
#endif
            break;
        }

      case Prefs::MAIN_WINDOW_X:
      case Prefs::MAIN_WINDOW_Y:
      case Prefs::MAIN_WINDOW_WIDTH:
      case Prefs::MAIN_WINDOW_HEIGHT:
        setGeometry (myPrefs.getInt (Prefs::MAIN_WINDOW_X),
                     myPrefs.getInt (Prefs::MAIN_WINDOW_Y),
                     myPrefs.getInt (Prefs::MAIN_WINDOW_WIDTH),
                     myPrefs.getInt (Prefs::MAIN_WINDOW_HEIGHT));
        break;

      case Prefs::ALT_SPEED_LIMIT_ENABLED:
      case Prefs::ALT_SPEED_LIMIT_UP:
      case Prefs::ALT_SPEED_LIMIT_DOWN:
        {
          b = myPrefs.getBool (Prefs::ALT_SPEED_LIMIT_ENABLED);
          myAltSpeedAction->setChecked (b);
          ui.altSpeedButton->setChecked (b);
          const QString fmt = b ? tr ("Click to disable Temporary Speed Limits\n (%1 down, %2 up)")
                                : tr ("Click to enable Temporary Speed Limits\n (%1 down, %2 up)");
          const Speed d = Speed::fromKBps (myPrefs.getInt (Prefs::ALT_SPEED_LIMIT_DOWN));
          const Speed u = Speed::fromKBps (myPrefs.getInt (Prefs::ALT_SPEED_LIMIT_UP));
          ui.altSpeedButton->setToolTip (fmt.arg (Formatter::speedToString (d))
                                            .arg (Formatter::speedToString (u)));
          break;
        }

      default:
        break;
    }
}

/***
****
***/

namespace
{
  const QLatin1String SHOW_OPTIONS_CHECKBOX_NAME ("show-options-checkbox");
}

void
MainWindow::newTorrent ()
{
  MakeDialog * dialog = new MakeDialog (mySession, this);
  dialog->setAttribute (Qt::WA_DeleteOnClose);
  dialog->show ();
}

void
MainWindow::openTorrent ()
{
  QFileDialog * d;
  d = new QFileDialog (this,
                       tr ("Open Torrent"),
                       myPrefs.getString (Prefs::OPEN_DIALOG_FOLDER),
                       tr ("Torrent Files (*.torrent);;All Files (*.*)"));
  d->setFileMode (QFileDialog::ExistingFiles);
  d->setAttribute (Qt::WA_DeleteOnClose);

  QCheckBox * b = new QCheckBox (tr ("Show &options dialog"));
  b->setChecked (myPrefs.getBool (Prefs::OPTIONS_PROMPT));
  b->setObjectName (SHOW_OPTIONS_CHECKBOX_NAME);
  auto l = qobject_cast<QGridLayout*> (d->layout ());
  if (l == nullptr)
    {
      l = new QGridLayout;
      d->setLayout (l);
    }
  l->addWidget (b, l->rowCount(), 0, 1, -1, Qt::AlignLeft);

  connect (d, SIGNAL (filesSelected (QStringList)),
           this, SLOT (addTorrents (QStringList)));

  d->show ();
}

void
MainWindow::openURL ()
{
  QString str = qApp->clipboard ()->text (QClipboard::Selection);

  if (!AddData::isSupported (str))
    str = qApp->clipboard ()->text (QClipboard::Clipboard);

  if (!AddData::isSupported (str))
    str.clear ();

  addTorrent (str, true);
}

void
MainWindow::addTorrents (const QStringList& filenames)
{
  bool showOptions = myPrefs.getBool (Prefs::OPTIONS_PROMPT);

  const QFileDialog * const fileDialog = qobject_cast<const QFileDialog*> (sender ());
  if (fileDialog != NULL)
    {
      const QCheckBox * const b = fileDialog->findChild<const QCheckBox*> (SHOW_OPTIONS_CHECKBOX_NAME);
      if (b != NULL)
        showOptions = b->isChecked ();
    }

  for (const QString& filename: filenames)
    addTorrent (filename, showOptions);
}

void
MainWindow::addTorrent (const AddData& addMe, bool showOptions)
{
  if (showOptions)
    {
      OptionsDialog * o = new OptionsDialog (mySession, myPrefs, addMe, this);
      o->show ();
      qApp->alert (o);
    }
  else
    {
      mySession.addTorrent (addMe);
      qApp->alert (this);
    }
}

void
MainWindow::removeTorrents (const bool deleteFiles)
{
  QSet<int> ids;
  QMessageBox msgBox (this);
  QString primary_text, secondary_text;
  int incomplete = 0;
  int connected  = 0;
  int count;

  for (const QModelIndex& index: ui.listView->selectionModel ()->selectedRows ())
    {
      const Torrent * tor (index.data (TorrentModel::TorrentRole).value<const Torrent*> ());
      ids.insert (tor->id ());

      if (tor->connectedPeers ())
        ++connected;

      if (!tor->isDone ())
        ++incomplete;
    }

  if (ids.isEmpty ())
    return;

  count = ids.size ();

  if (!deleteFiles)
    {
      primary_text = (count == 1)
        ? tr ("Remove torrent?")
        : tr ("Remove %Ln torrent(s)?", 0, count);
    }
  else
    {
      primary_text = (count == 1)
        ? tr ("Delete this torrent's downloaded files?")
        : tr ("Delete these %Ln torrent(s)' downloaded files?", 0, count);
    }

  if (!incomplete && !connected)
    {
      secondary_text = (count == 1)
        ? tr ("Once removed, continuing the transfer will require the torrent file or magnet link.")
        : tr ("Once removed, continuing the transfers will require the torrent files or magnet links.");
    }
  else if (count == incomplete)
    {
      secondary_text = (count == 1)
        ? tr ("This torrent has not finished downloading.")
        : tr ("These torrents have not finished downloading.");
    }
  else if (count == connected)
    {
      secondary_text = (count == 1)
        ? tr ("This torrent is connected to peers.")
        : tr ("These torrents are connected to peers.");
    }
  else
    {
      if (connected)
        {
          secondary_text = (connected == 1)
            ? tr ("One of these torrents is connected to peers.")
            : tr ("Some of these torrents are connected to peers.");
        }

      if (connected && incomplete)
        {
          secondary_text += QLatin1Char ('\n');
        }

      if (incomplete)
        {
          secondary_text += (incomplete == 1)
            ? tr ("One of these torrents has not finished downloading.")
            : tr ("Some of these torrents have not finished downloading.");
        }
    }

  msgBox.setWindowTitle (QLatin1String (" "));
  msgBox.setText (QString::fromLatin1 ("<big><b>%1</big></b>").arg (primary_text));
  msgBox.setInformativeText (secondary_text);
  msgBox.setStandardButtons (QMessageBox::Ok | QMessageBox::Cancel);
  msgBox.setDefaultButton (QMessageBox::Cancel);
  msgBox.setIcon (QMessageBox::Question);
  // hack needed to keep the dialog from being too narrow
  auto layout = qobject_cast<QGridLayout*> (msgBox.layout ());
  if (layout == nullptr)
    {
      layout = new QGridLayout;
      msgBox.setLayout (layout);
    }
  QSpacerItem* spacer = new QSpacerItem (450, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
  layout->addItem (spacer, layout->rowCount (), 0, 1, layout->columnCount ());

  if (msgBox.exec () == QMessageBox::Ok)
    {
      ui.listView->selectionModel ()->clear ();
      mySession.removeTorrents (ids, deleteFiles);
    }
}

/***
****
***/

void
MainWindow::updateNetworkIcon ()
{
  const time_t now = time (NULL);
  const int period = 3;
  const time_t secondsSinceLastSend = now - myLastSendTime;
  const time_t secondsSinceLastRead = now - myLastReadTime;
  const bool isSending = secondsSinceLastSend <= period;
  const bool isReading = secondsSinceLastRead <= period;
  const char * key;

  if (myNetworkError)
    key = "network-error";
  else if (isSending && isReading)
    key = "network-transmit-receive";
  else if (isSending)
    key = "network-transmit";
  else if (isReading)
    key = "network-receive";
  else
    key = "network-idle";
  const QIcon icon = getStockIcon (QLatin1String (key), QStyle::SP_DriveNetIcon);
  const QPixmap pixmap = icon.pixmap (16, 16);

  QString tip;
  const QString url = mySession.getRemoteUrl ().host ();
  if (!myLastReadTime)
    tip = tr ("%1 has not responded yet").arg (url);
  else if (myNetworkError)
    tip = tr (myErrorMessage.toLatin1 ().constData ());
  else if (secondsSinceLastRead < 30)
    tip = tr ("%1 is responding").arg (url);
  else if (secondsSinceLastRead < (60*2))
    tip = tr ("%1 last responded %2 ago").arg (url).arg (Formatter::timeToString (secondsSinceLastRead));
  else
    tip = tr ("%1 is not responding").arg (url);

  ui.networkLabel->setPixmap (pixmap);
  ui.networkLabel->setToolTip (tip);
}

void
MainWindow::onNetworkTimer ()
{
  updateNetworkIcon ();
}

void
MainWindow::dataReadProgress ()
{
  if (!myNetworkError)
  myLastReadTime = time (NULL);
}

void
MainWindow::dataSendProgress ()
{
  myLastSendTime = time (NULL);
}

void
MainWindow::onError (QNetworkReply::NetworkError code)
{
  const bool hadError = myNetworkError;
  const bool haveError = (code != QNetworkReply::NoError)
                      && (code != QNetworkReply::UnknownContentError);

  myNetworkError = haveError;
  refreshTrayIconSoon();
  updateNetworkIcon();

  // Refresh our model if we've just gotten a clean connection to the session.
  // That way we can rebuild after a restart of transmission-daemon
  if (hadError && !haveError)
    myModel.clear();
}

void
MainWindow::errorMessage (const QString& msg)
{
    myErrorMessage = msg;
}

void
MainWindow::wrongAuthentication ()
{
  mySession.stop ();
  openSession ();
}

/***
****
***/

void
MainWindow::dragEnterEvent (QDragEnterEvent * event)
{
  const QMimeData * mime = event->mimeData ();

  if (mime->hasFormat (QLatin1String ("application/x-bittorrent"))
        || mime->hasUrls()
        || mime->text ().trimmed ().endsWith (QLatin1String (".torrent"), Qt::CaseInsensitive)
        || mime->text ().startsWith (QLatin1String ("magnet:"), Qt::CaseInsensitive))
    event->acceptProposedAction ();
}

void
MainWindow::dropEvent (QDropEvent * event)
{
  QStringList list;

  if (event->mimeData()->hasText())
    {
      list = event->mimeData()->text().trimmed().split(QLatin1Char ('\n'));
    }
  else if (event->mimeData()->hasUrls())
    {
      for (const QUrl& url: event->mimeData()->urls())
        list.append(url.toLocalFile());
    }

  for (const QString& entry: list)
    {
      QString key = entry.trimmed();

      if (!key.isEmpty())
        {
          const QUrl url (key);

          if (url.scheme () == QLatin1String ("file"))
            key = QUrl::fromPercentEncoding (url.path().toUtf8());

          qApp->addTorrent (key);
        }
    }
}

/***
****
***/

void
MainWindow::contextMenuEvent (QContextMenuEvent * event)
{
  QMenu * menu = new QMenu (this);

  menu->addAction (ui.action_Properties);
  menu->addAction (ui.action_OpenFolder);

  QAction * sep = new QAction (menu);
  sep->setSeparator (true);
  menu->addAction (sep);
  menu->addAction (ui.action_Start);
  menu->addAction (ui.action_StartNow);
  menu->addAction (ui.action_Announce);
  QMenu * queueMenu = menu->addMenu (tr ("Queue"));
    queueMenu->addAction (ui.action_QueueMoveTop);
    queueMenu->addAction (ui.action_QueueMoveUp);
    queueMenu->addAction (ui.action_QueueMoveDown);
    queueMenu->addAction (ui.action_QueueMoveBottom);
  menu->addAction (ui.action_Pause);

  sep = new QAction (menu);
  sep->setSeparator (true);
  menu->addAction (sep);
  menu->addAction (ui.action_Verify);
  menu->addAction (ui.action_SetLocation);
  menu->addAction (ui.action_CopyMagnetToClipboard);

  sep = new QAction (menu);
  sep->setSeparator (true);
  menu->addAction (sep);
  menu->addAction (ui.action_Remove);
  menu->addAction (ui.action_Delete);

  menu->popup (event->globalPos ());
}