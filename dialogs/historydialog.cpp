#include "historydialog.h"
#include "ui_historydialog.h"

#include "../tools/os.h"
#include "../tools/uploader.h"
#include "../tools/screenshotmanager.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QUrl>

#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlTableModel>

#include <QDebug>

HistoryDialog::HistoryDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::HistoryDialog)
{
  ui->setupUi(this);

  ui->filterEdit->setText(tr("Filter.."));
  ui->filterEdit->installEventFilter(this);

  if (ScreenshotManager::instance()->history().isOpen())
  {
    QSqlTableModel *model = new QSqlTableModel(this, ScreenshotManager::instance()->history());
    model->setTable("history");
    model->setHeaderData(0, Qt::Horizontal, tr("Screenshot"));
    model->setHeaderData(1, Qt::Horizontal, tr("URL"));
    model->select();

    mFilterModel = new QSortFilterProxyModel(model);
    mFilterModel->setSourceModel(model);
    mFilterModel->setDynamicSortFilter(true);
    mFilterModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    mFilterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    mFilterModel->setFilterKeyColumn(-1);

    ui->tableView->setModel(mFilterModel);

    ui->tableView->hideColumn(2); // No delete hash.
    ui->tableView->hideColumn(3); // No timestamp.

    ui->tableView->horizontalHeader()->setClickable(false);
    ui->tableView->horizontalHeader()->setMovable(false);

    ui->tableView->horizontalHeader()->setResizeMode(0, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(1, QHeaderView::ResizeToContents);

    ui->tableView->verticalHeader()->hide();

    ui->tableView->setTextElideMode(Qt::ElideLeft);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tableView->setSortingEnabled(true);

    if (ui->tableView->model()->rowCount() > 0)
    {
      ui->clearButton->setEnabled(true);
      ui->filterEdit->setEnabled(true);
    }

    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(selectionChanged(QItemSelection,QItemSelection)));
    connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(open(QModelIndex)));
    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextMenu(QPoint)));
  }
  else {
    ui->tableView->setEnabled(false);
    ui->clearButton->setEnabled(false);
  }

  ui->uploadProgressBar->setValue  (Uploader::instance()->progressSent());

  if (Uploader::instance()->progressTotal() == 0) {
    ui->uploadProgressBar->setMaximum(1);
  }
  else {
    ui->uploadProgressBar->setMaximum(Uploader::instance()->progressTotal());
  }

  connect(Uploader::instance(), SIGNAL(progress(qint64,qint64)), this, SLOT(uploadProgress(qint64, qint64)));
  connect(ui->uploadButton, SIGNAL(clicked()), this, SLOT(upload()));
  connect(ui->clearButton , SIGNAL(clicked()), this, SLOT(clear()));
}

HistoryDialog::~HistoryDialog()
{
  delete ui;
}

void HistoryDialog::clear()
{
  if (QMessageBox::question(this,
                            tr("Clearing the screenshot history"),
                            tr("Are you sure you want to clear your entire screenshot history?\nThis cannot be undone."),
                            tr("Clear History"),
                            tr("Don't Clear")) == 1) {
    return;
  }

  ScreenshotManager::instance()->clearHistory();
  close();
}

void HistoryDialog::contextMenu(QPoint point)
{
  mContextIndex = ui->tableView->indexAt(point);;

  QMenu contextMenu(ui->tableView);

  QAction copyAction((mContextIndex.column() == 0) ? tr("Copy Path") : tr("Copy URL"), &contextMenu);
  connect(&copyAction, SIGNAL(triggered()), this, SLOT(copy()));
  contextMenu.addAction(&copyAction);

  QAction deleteAction(tr("Delete from imgur.com"), &contextMenu);
  QAction locationAction(tr("Open Location"), &contextMenu);

  if (mContextIndex.column() == 0)
  {
    connect(&locationAction, SIGNAL(triggered()), this, SLOT(location()));
    contextMenu.addAction(&locationAction);
  }
  else {
    connect(&deleteAction, SIGNAL(triggered()), this, SLOT(deleteImage()));
    contextMenu.addAction(&deleteAction);
  }

  if (mContextIndex.data().toString().isEmpty()) {
    copyAction.setEnabled(false);
    deleteAction.setEnabled(false);
  }

  contextMenu.exec(QCursor::pos());
}

void HistoryDialog::copy()
{
  qApp->clipboard()->setText(mContextIndex.data().toString());
}

void HistoryDialog::deleteImage()
{
  QDesktopServices::openUrl(mContextIndex.sibling(mContextIndex.row(), 2).data().toString());
}

void HistoryDialog::location()
{
  QDesktopServices::openUrl("file:///" + QFileInfo(mContextIndex.data().toString()).absolutePath());
}

void HistoryDialog::open(QModelIndex index)
{
  if (index.column() == 0) {
    QDesktopServices::openUrl(QUrl("file:///" + index.data().toString()));
  }
  else {
    QDesktopServices::openUrl(index.data().toUrl());
  }
}

void HistoryDialog::reloadHistory()
{
  qobject_cast<QSqlTableModel*>(mFilterModel->sourceModel())->select();
}

void HistoryDialog::selectionChanged(QItemSelection selected, QItemSelection deselected)
{
  Q_UNUSED(deselected);

  QModelIndex index = selected.indexes().at(0);

  QString screenshot, url;

  if (index.column() == 0) {
    screenshot = index.data().toString();
    url = ui->tableView->model()->index(index.row(), 1).data().toString();
  }
  else {
    screenshot = ui->tableView->model()->index(index.row(), 0).data().toString();
    url = index.data().toString();
  }

  mSelectedScreenshot = screenshot;

  ui->uploadButton->setEnabled((url.isEmpty() && QFile::exists(screenshot)));
}

void HistoryDialog::upload()
{
  Uploader::instance()->upload(mSelectedScreenshot);
  ui->uploadButton->setEnabled(false);
}

void HistoryDialog::uploadProgress(qint64 sent, qint64 total)
{
  ui->uploadProgressBar->setEnabled(true);

  ui->uploadProgressBar->setMaximum(total);
  ui->uploadProgressBar->setValue(sent);
}

bool HistoryDialog::eventFilter(QObject *object, QEvent *event)
{
  if (object == ui->filterEdit) {
    if (event->type() == QEvent::FocusIn)
    {
      if (ui->filterEdit->text() == tr("Filter..")) {
        ui->filterEdit->setStyleSheet("");
        ui->filterEdit->setText("");
        mFilterModel->setFilterWildcard("");
        mFilterModel->sort(3, Qt::DescendingOrder);
      }
    }
    else if (event->type() == QEvent::FocusOut)
    {
      if (ui->filterEdit->text() == "") {
        ui->filterEdit->setStyleSheet("color: palette(mid);");
        ui->filterEdit->setText(tr("Filter.."));
        mFilterModel->sort(3, Qt::DescendingOrder);
      }
    }
    else if (event->type() == QEvent::KeyRelease)
    {
      if (ui->filterEdit->text() != tr("Filter..") && !ui->filterEdit->text().isEmpty()) {
        mFilterModel->setFilterWildcard(ui->filterEdit->text());
        mFilterModel->sort(3, Qt::DescendingOrder);
      }
      else {
        mFilterModel->setFilterWildcard("");
        mFilterModel->sort(3, Qt::DescendingOrder);
      }
    }
  }
  return QDialog::eventFilter(object, event);
}
