// clang-format off
/*
 * KDiff3 - Text Diff And Merge Tool
 *
 * SPDX-FileCopyrightText: 2002-2011 Joachim Eibl, joachim.eibl at gmx.de
 * SPDX-FileCopyrightText: 2018-2020 Michael Reeves reeves.87@gmail.com
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
// clang-format on

#include "kdiff3_shell.h"
#include "compat.h"
#include "kdiff3.h"

#include <QApplication>
#include <QCloseEvent>
#include <QStatusBar>

#include <KConfig>
#include <KEditToolBar>
#include <KLocalizedString>
#include <KMessageBox>
#include <KShortcutsDialog>
#include <KStandardAction>
#include <KToolBar>

KDiff3Shell::KDiff3Shell(const QString& fn1, const QString& fn2, const QString& fn3)
{
    m_bUnderConstruction = true;

    m_widget = new KDiff3App(this, u8"KDiff3Part", this);

    if(m_widget)
    {
        setupGUI(Default, "kdiff3_shell.rc");
        // and a status bar
        statusBar()->show();
        //toolBar()->setToolButtonStyle( Qt::ToolButtonIconOnly );

        setCentralWidget(m_widget);

        m_widget->completeInit(fn1, fn2, fn3);
        chk_connect_a(m_widget, &KDiff3App::createNewInstance, this, &KDiff3Shell::slotNewInstance);

        // apply the saved mainwindow settings, if any, and ask the mainwindow
        // to automatically save settings if changed: window size, toolbar
        // position, icon size, etc.
        setAutoSaveSettings();
        m_bUnderConstruction = false;
    }
    else
    {
        // if we couldn't find our Part, we exit since the Shell by
        // itself can't do anything useful
        KMessageBox::error(this, i18n("Could not initialize the KDiff3 part.\n"
                                      "This usually happens due to an installation problem. "
                                      "Please read the README-file in the source package for details."));

        ::exit(-1); //QApplication::quit() doesn't work here yet.

        return;
    }
}

KDiff3Shell::~KDiff3Shell() = default;

bool KDiff3Shell::queryClose()
{
    if(m_widget)
        return m_widget->queryClose();
    else
        return true;
}

bool KDiff3Shell::queryExit()
{
    return true;
}

void KDiff3Shell::closeEvent(QCloseEvent* e)
{
    if(queryClose())
    {
        e->accept();
        bool bFileSaved = m_widget->isFileSaved();
        bool bDirCompare = m_widget->isDirComparison();
        QApplication::exit(bFileSaved || bDirCompare ? 0 : 1);
    }
    else
        e->ignore();
}

void KDiff3Shell::optionsShowToolbar()
{
    // this is all very cut and paste code for showing/hiding the
    // toolbar
    if(m_toolbarAction->isChecked())
        toolBar()->show();
    else
        toolBar()->hide();
}

void KDiff3Shell::optionsShowStatusbar()
{
    // this is all very cut and paste code for showing/hiding the
    // statusbar
    if(m_statusbarAction->isChecked())
        statusBar()->show();
    else
        statusBar()->hide();
}

void KDiff3Shell::optionsConfigureKeys()
{
#if KF_VERSION < KF_VERSION_CHECK(5, 84, 0)
    KShortcutsDialog::configure(actionCollection() /*, "kdiff3_shell.rc" */);
#else
    KShortcutsDialog::showDialog(actionCollection());
#endif
}

void KDiff3Shell::optionsConfigureToolbars()
{
    KConfigGroup mainWindowGroup(KSharedConfig::openConfig(), "MainWindow");
    saveMainWindowSettings(mainWindowGroup);

    // use the standard toolbar editor
    KEditToolBar dlg(factory());
    chk_connect_a(&dlg, &KEditToolBar::newToolBarConfig, this, &KDiff3Shell::applyNewToolbarConfig);
    dlg.exec();
}

void KDiff3Shell::applyNewToolbarConfig()
{
    KConfigGroup mainWindowGroup(KSharedConfig::openConfig(), "MainWindow");
    applyMainWindowSettings(mainWindowGroup);
}

void KDiff3Shell::slotNewInstance(const QString& fn1, const QString& fn2, const QString& fn3)
{
    static KDiff3Shell* pKDiff3Shell = new KDiff3Shell(fn1, fn2, fn3);
    Q_UNUSED(pKDiff3Shell);
}

//#include "kdiff3_shell.moc"
