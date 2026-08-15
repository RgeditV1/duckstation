// SPDX-FileCopyrightText: 2019-2026 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#include "coverdownloadwindow.h"
#include "gamelistwidget.h"
#include "mainwindow.h"
#include "qthost.h"
#include "qtprogresscallback.h"

#include "core/core.h"
#include "core/game_list.h"

#include "common/error.h"
#include "common/string_util.h"

#include "moc_coverdownloadwindow.cpp"

using namespace Qt::StringLiterals;

CoverDownloadWindow::CoverDownloadWindow() : QWidget()
{
  m_ui.setupUi(this);
  m_ui.coverIcon->setPixmap(QIcon(u":/icons/monochrome/svg/artboard-2-line.svg"_s).pixmap(32));
  updateEnabled();

  connect(m_ui.start, &QPushButton::clicked, this, &CoverDownloadWindow::onStartClicked);
  connect(m_ui.close, &QPushButton::clicked, this, &CoverDownloadWindow::close);
  connect(m_ui.urls, &QPlainTextEdit::textChanged, this, &CoverDownloadWindow::updateEnabled);

  const std::vector<std::string> urls = Core::GetBaseStringListSetting("UI", "CoverDownloaderURL");
  if (!urls.empty())
    m_ui.urls->setPlainText(QString::fromStdString(StringUtil::JoinString(urls, "\n")));
}

CoverDownloadWindow::~CoverDownloadWindow() = default;

void CoverDownloadWindow::closeEvent(QCloseEvent* ev)
{
  std::vector<std::string> urls;
  for (const QString& str : m_ui.urls->toPlainText().split(QChar('\n')))
  {
    std::string url = str.toStdString();
    StringUtil::StripWhitespace(&url);
    if (!url.empty())
      urls.push_back(std::move(url));
  }
  if (urls != Core::GetBaseStringListSetting("UI", "CoverDownloaderURL"))
    Core::SetBaseStringListSettingValue("UI", "CoverDownloaderURL", urls);

  QtUtils::SaveWindowGeometry(this);
  QWidget::closeEvent(ev);
  if (m_task)
    m_task->cancel();
  emit closed();
}

void CoverDownloadWindow::onStartClicked()
{
  if (m_task)
  {
    m_task->cancel();
    return;
  }

  std::vector<std::string> urls;
  const bool use_serials = m_ui.useSerialFileNames->isChecked();
  for (const QString& str : m_ui.urls->toPlainText().split(QChar('\n')))
    urls.push_back(str.toStdString());

  m_task = QtAsyncTaskWithProgress::create(
    this, [this, urls = std::move(urls), use_serials](ProgressCallback* const progress) {
      Error error;
      u32 downloaded_count = 0;
      bool timed_out = false;
      bool network_error = false;
      const bool result = GameList::DownloadCovers(
        urls, use_serials, progress, &error, [](const GameList::Entry* entry, std::string) mutable {
          Host::RunOnUIThread([path = entry->path]() {
            g_main_window->getGameListWidget()->getModel()->invalidateColumnForPath(path, GameListModel::Column_Cover);
          });
        },
        &downloaded_count, &timed_out, &network_error);
      const bool cancelled = progress->IsCancelled();
      return [this, result, error = std::move(error), downloaded_count, cancelled, timed_out, network_error]() {
        downloadComplete(result, error, downloaded_count, cancelled, timed_out, network_error);
      };
    });

  m_task->connectWidgets(m_ui.status, m_ui.progress, m_ui.start);

  m_task->start();

  updateEnabled();
}

void CoverDownloadWindow::downloadComplete(bool result, const Error& error, u32 downloaded_count, bool cancelled,
                                           bool timed_out, bool network_error)
{
  g_main_window->getGameListWidget()->getModel()->invalidateColumn(GameListModel::Column_Cover);

  if (!result)
  {
    if (const std::string& err_str = error.GetDescription(); !err_str.empty())
      m_ui.status->setText(QString::fromStdString(err_str));
  }
  else if (cancelled)
  {
    m_ui.status->setText(tr("Download cancelled."));
  }
  else if (timed_out)
  {
    m_ui.status->setText(tr("Download timed out. Check your internet connection and try again."));
  }
  else if (network_error)
  {
    m_ui.status->setText(tr("Network error. Check your internet connection and try again."));
  }
  else if (downloaded_count == 0)
  {
    m_ui.status->setText(tr("No covers found."));
  }
  else
  {
    m_ui.status->setText(tr("Download complete."));
  }

  m_task = nullptr;
  updateEnabled();
}

void CoverDownloadWindow::updateEnabled()
{
  const bool running = static_cast<bool>(m_task);
  m_ui.start->setText(running ? tr("Stop") : tr("Start"));
  m_ui.start->setEnabled(running || !m_ui.urls->toPlainText().isEmpty());
  m_ui.close->setEnabled(!running);
  m_ui.urls->setEnabled(!running);
}
