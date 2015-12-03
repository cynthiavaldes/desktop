/*
 * Copyright (C) by Daniel Molkentin <danimo@owncloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "folderman.h"
#include "theme.h"
#include "generalsettings.h"
#include "networksettings.h"
#include "accountsettings.h"
#include "configfile.h"
#include "progressdispatcher.h"
#include "owncloudgui.h"
#include "activitywidget.h"
#include "accountmanager.h"
#include "protocolwidget.h"

#include <QLabel>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QPushButton>
#include <QDebug>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QImage>
#include <QWidgetAction>

namespace {
  const char TOOLBAR_CSS[] =
    "QToolBar { background: %1; margin: 0; padding: 0; border: none; border-bottom: 1px solid %2; spacing: 0; } "
    "QToolBar QToolButton { background: %1; border: none; border-bottom: 1px solid %2; margin: 0; padding: 5px; } "
    "QToolBar QToolBarExtension { padding:0; } "
    "QToolBar QToolButton:checked { background: %3; color: %4; }";

  static const float buttonSizeRatio = 1.618; // golden ratio
}


namespace OCC {

//
// Whenever you change something here check both settingsdialog.cpp and settingsdialogmac.cpp !
//

SettingsDialog::SettingsDialog(ownCloudGui *gui, QWidget *parent) :
    QDialog(parent)
    , _ui(new Ui::SettingsDialog), _gui(gui)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    _ui->setupUi(this);
    _toolBar = new QToolBar;
    _toolBar->setIconSize(QSize(32, 32));
    _toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    layout()->setMenuBar(_toolBar);

    // People perceive this as a Window, so also make Ctrl+W work
    QAction *closeWindowAction = new QAction(this);
    closeWindowAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(closeWindowAction, SIGNAL(triggered()), SLOT(accept()));
    addAction(closeWindowAction);

    setObjectName("Settings"); // required as group for saveGeometry call
    setWindowTitle(Theme::instance()->appNameGUI());

    _actionGroup = new QActionGroup(this);
    _actionGroup->setExclusive(true);

    // Note: all the actions have a '\n' because the account name is in two lines and
    // all buttons must have the same size in order to keep a good layout
    _activityAction = createColorAwareAction(QLatin1String(":/client/resources/activity.png"), tr("Activity"));
    _actionGroup->addAction(_activityAction);
    addActionToToolBar(_activityAction);
    _activitySettings = new ActivitySettings;
    _ui->stack->addWidget(_activitySettings);
    connect( _activitySettings, SIGNAL(guiLog(QString,QString)), _gui,
             SLOT(slotShowOptionalTrayMessage(QString,QString)) );

    QAction *generalAction = createColorAwareAction(QLatin1String(":/client/resources/settings.png"), tr("General"));
    _actionGroup->addAction(generalAction);
    _toolBar->addAction(generalAction);
    GeneralSettings *generalSettings = new GeneralSettings;
    _ui->stack->addWidget(generalSettings);

    QAction *networkAction = createColorAwareAction(QLatin1String(":/client/resources/network.png"), tr("Network"));
    _actionGroup->addAction(networkAction);
    _toolBar->addAction(networkAction);
    NetworkSettings *networkSettings = new NetworkSettings;
    _ui->stack->addWidget(networkSettings);

    _actionGroupWidgets.insert(_activityAction, _activitySettings);
    _actionGroupWidgets.insert(generalAction, generalSettings);
    _actionGroupWidgets.insert(networkAction, networkSettings);

    connect(_actionGroup, SIGNAL(triggered(QAction*)), SLOT(slotSwitchPage(QAction*)));

    connect(AccountManager::instance(), SIGNAL(accountAdded(AccountState*)),
            this, SLOT(accountAdded(AccountState*)));
    connect(AccountManager::instance(), SIGNAL(accountRemoved(AccountState*)),
            this, SLOT(accountRemoved(AccountState*)));
    foreach (auto ai , AccountManager::instance()->accounts()) {
        accountAdded(ai.data());
    }

    QTimer::singleShot(1, this, SLOT(showFirstPage()));

    QPushButton *closeButton = _ui->buttonBox->button(QDialogButtonBox::Close);
    connect(closeButton, SIGNAL(clicked()), SLOT(accept()));

    QAction *showLogWindow = new QAction(this);
    showLogWindow->setShortcut(QKeySequence("F12"));
    connect(showLogWindow, SIGNAL(triggered()), gui, SLOT(slotToggleLogBrowser()));
    addAction(showLogWindow);

    customizeStyle();

    ConfigFile cfg;
    cfg.restoreGeometry(this);
}

SettingsDialog::~SettingsDialog()
{
    delete _ui;
}

// close event is not being called here
void SettingsDialog::reject() {
    ConfigFile cfg;
    cfg.saveGeometry(this);
    QDialog::reject();
}

void SettingsDialog::accept() {
    ConfigFile cfg;
    cfg.saveGeometry(this);
    QDialog::accept();
}

void SettingsDialog::changeEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    case QEvent::ThemeChange:
#endif
        customizeStyle();
        break;
    default:
        break;
    }

    QDialog::changeEvent(e);
}

void SettingsDialog::slotSwitchPage(QAction *action)
{
    _ui->stack->setCurrentWidget(_actionGroupWidgets.value(action));
}

void SettingsDialog::showFirstPage()
{
    QList<QAction*> actions = _toolBar->actions();
    if (!actions.empty()) {
        actions.first()->trigger();
    }
}

void SettingsDialog::showActivityPage()
{
    if (_activityAction) {
        _activityAction->trigger();
    }
}

void SettingsDialog::accountAdded(AccountState *s)
{
    auto height = _toolBar->sizeHint().height();
    auto accountAction = createColorAwareAction(QLatin1String(":/client/resources/account.png"),
                                                s->account()->displayName());
    accountAction->setToolTip(s->account()->displayName());
    accountAction->setIconText(s->shortDisplayNameForSettings(height * buttonSizeRatio));
    _toolBar->insertAction(_toolBar->actions().at(0), accountAction);
    auto accountSettings = new AccountSettings(s, this);
    _ui->stack->insertWidget(0 , accountSettings);
    _actionGroup->addAction(accountAction);
    _actionGroupWidgets.insert(accountAction, accountSettings);

    connect( accountSettings, SIGNAL(folderChanged()), _gui, SLOT(slotFoldersChanged()));
    connect( accountSettings, SIGNAL(openFolderAlias(const QString&)),
             _gui, SLOT(slotFolderOpenAction(QString)));

    slotRefreshActivity(s);
}

void SettingsDialog::accountRemoved(AccountState *s)
{
    for (auto it = _actionGroupWidgets.begin(); it != _actionGroupWidgets.end(); ++it) {
        auto as = qobject_cast<AccountSettings *>(*it);
        if (!as) {
            continue;
        }
        if (as->accountsState() == s) {
            _toolBar->removeAction(it.key());

            it.key()->deleteLater();
            it.value()->deleteLater();
            _actionGroupWidgets.erase(it);

            if (_ui->stack->currentWidget() == it.value()) {
                showFirstPage();
            }
            break;
        }
    }

    _activitySettings->slotRemoveAccount(s);
}

void SettingsDialog::customizeStyle()
{
    QString  highlightColor(palette().highlight().color().name());
    QString altBase(palette().alternateBase().color().name());
    QString dark(palette().dark().color().name());
    QString background(palette().base().color().name());
    _toolBar->setStyleSheet(QString::fromAscii(TOOLBAR_CSS).arg(background).arg(dark).arg(highlightColor).arg(altBase));

    Q_FOREACH(QAction *a, _actionGroup->actions()) {
        QIcon icon = createColorAwareIcon(a->property("iconPath").toString());
        a->setIcon(icon);
        QToolButton *btn = qobject_cast<QToolButton*>(_toolBar->widgetForAction(a));
        if (btn) {
            btn->setIcon(icon);
        }
    }

}

QIcon SettingsDialog::createColorAwareIcon(const QString &name)
{
    QColor  bg(palette().base().color());
    QImage img(name);
    // account for different sensitivity of the human eye to certain colors
    double treshold = 1.0 - ( 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue())/255.0;
    if (treshold > 0.5) {
        img.invertPixels(QImage::InvertRgb);
    }

    return QIcon(QPixmap::fromImage(img));
}

class ToolButtonAction : public QWidgetAction
{
public:
    explicit ToolButtonAction(const QIcon &icon, const QString &text, QObject* parent)
            : QWidgetAction(parent)  {
        setText(text);
        setIcon(icon);
    }


    QWidget* createWidget(QWidget* parent) Q_DECL_OVERRIDE {

        auto toolbar = qobject_cast<QToolBar*>(parent);
        if (!toolbar) {
            // this means we are in the extention menu, no special action here
            return 0;
        }

        QToolButton* btn = new QToolButton(parent);
        btn->setDefaultAction(this);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
//         btn->setMinimumWidth(qMax<int>(parent->sizeHint().height() * buttonSizeRatio,
//                                        btn->sizeHint().width()));
        return btn;
    }
};

QAction *SettingsDialog::createColorAwareAction(const QString &iconPath, const QString &text)
{
    // all buttons must have the same size in order to keep a good layout
    QIcon coloredIcon = createColorAwareIcon(iconPath);
    QAction *action = new ToolButtonAction(coloredIcon, text, this);
    action->setCheckable(true);
    action->setProperty("iconPath", iconPath);
    return action;
}

void SettingsDialog::addActionToToolBar(QAction *action) {
    QToolButton* btn = new QToolButton;
    btn->setDefaultAction(action);
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    _toolBar->addWidget(btn);
    btn->setMinimumWidth(_toolBar->sizeHint().height() * 1.3);
}

void SettingsDialog::slotRefreshActivity( AccountState* accountState )
{
    if (accountState) {
        _activitySettings->slotRefresh(accountState);
    }
}

} // namespace OCC
