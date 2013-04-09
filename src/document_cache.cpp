/***********************************************************************
 *
 * Copyright (C) 2012, 2013 Graeme Gott <graeme@gottcode.org>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ***********************************************************************/

#include "document_cache.h"

#include "document.h"
#include "document_writer.h"
#include "stack.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

//-----------------------------------------------------------------------------

QString DocumentCache::m_path;

//-----------------------------------------------------------------------------

DocumentCache::DocumentCache(QObject* parent) :
	QObject(parent),
	m_ordering(0)
{
	QStringList entries = QDir(m_path).entryList(QDir::Files);
	if ((entries.count() >= 1) && entries.contains("mapping")) {
		m_previous_cache = backupCache();
	}
}

//-----------------------------------------------------------------------------

DocumentCache::~DocumentCache()
{
	backupCache();
}

//-----------------------------------------------------------------------------

bool DocumentCache::isClean() const
{
	return m_previous_cache.isEmpty();
}

//-----------------------------------------------------------------------------

bool DocumentCache::isWritable() const
{
	return QFileInfo(m_path).isWritable() && QFileInfo(m_path + "/../").isWritable();
}

//-----------------------------------------------------------------------------

void DocumentCache::parseMapping(QStringList& files, QStringList& datafiles) const
{
	QString cache_path = isClean() ? m_path : m_previous_cache;
	QFile file(cache_path + "/mapping");
	if (file.open(QFile::ReadOnly | QFile::Text)) {
		QTextStream stream(&file);
		stream.setCodec("UTF-8");
		stream.setAutoDetectUnicode(true);

		while (!stream.atEnd()) {
			QString line = stream.readLine();
			QString datafile = line.section(' ', 0, 0);
			QString path = line.section(' ', 1);
			if (!datafile.isEmpty()) {
				files.append(path);
				datafiles.append(cache_path + "/" + datafile);
			}
		}
		file.close();
	}
}

//-----------------------------------------------------------------------------

void DocumentCache::add(Document* document)
{
	m_filenames[document] = createFileName();
	connect(document, SIGNAL(changedName()), this, SLOT(updateMapping()));
	connect(document, SIGNAL(replaceCacheFile(Document*, QString)), this, SLOT(replaceCacheFile(Document*, QString)));
	connect(document, SIGNAL(writeCacheFile(Document*, DocumentWriter*)), this, SLOT(writeCacheFile(Document*, DocumentWriter*)));
	updateMapping();
}

//-----------------------------------------------------------------------------

void DocumentCache::remove(Document* document)
{
	if (m_filenames.contains(document)) {
		QString cache_file = m_filenames.take(document);
		updateMapping();
		QFile::remove(cache_file);
	}
}

//-----------------------------------------------------------------------------

void DocumentCache::setOrdering(Stack* ordering)
{
	m_ordering = ordering;
}

//-----------------------------------------------------------------------------

void DocumentCache::setPath(const QString& path)
{
	m_path = path;
	if (!m_path.endsWith(QLatin1Char('/'))) {
		m_path += QLatin1Char('/');
	}
}

//-----------------------------------------------------------------------------

void DocumentCache::updateMapping()
{
	QFile file(m_path + "/mapping");
	if (file.open(QFile::WriteOnly | QFile::Text)) {
		QTextStream stream(&file);
		stream.setCodec("UTF-8");
		stream.setGenerateByteOrderMark(true);

		for (int i = 0; i < m_ordering->count(); ++i) {
			Document* document = m_ordering->document(i);
			if (!m_filenames.contains(document)) {
				continue;
			}
			stream << QFileInfo(m_filenames[document]).baseName() << " " << document->filename() << endl;
		}
		file.close();
	}
}

//-----------------------------------------------------------------------------

void DocumentCache::replaceCacheFile(Document* document, const QString& file)
{
	QString cache_file = m_path + m_filenames[document];
	if (cache_file == file) {
		return;
	}
	if (QFile::exists(cache_file)) {
		QFile::remove(cache_file);
	}
	QFile::copy(file, cache_file);
}

//-----------------------------------------------------------------------------

void DocumentCache::writeCacheFile(Document* document, DocumentWriter* writer)
{
	QString cache_file = m_path + m_filenames[document];
	writer->setFileName(cache_file);
	writer->write();
	delete writer;
}

//-----------------------------------------------------------------------------

QString DocumentCache::backupCache()
{
	// Find backup location
#if (QT_VERSION >= QT_VERSION_CHECK(4,7,0))
	QString date = QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmss");
#else
	QString date = QDateTime::currentDateTime().toUTC().toString("yyyyMMddhhmmss");
#endif
	int extra = 0;
	QDir dir(QDir::cleanPath(m_path + "/../"));
	QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::LocaleAware);
	subdirs.removeAll("Files");
	foreach (const QString& subdir, subdirs) {
		if (subdir.startsWith(date)) {
			extra = qMax(extra, subdir.mid(15).toInt() + 1);
		}
	}
	QString cachepath = dir.absoluteFilePath(date + ((extra == 0) ? "" : QString("-%1").arg(extra)));

	// Move cache files to backup location
	dir.rename("Files", cachepath);
	dir.mkdir("Files");

	// Limit to five backups
	while (subdirs.count() > 4) {
		QString subdir_name = subdirs.takeAt(0);
		QDir subdir(dir.absoluteFilePath(subdir_name));
#if (QT_VERSION >= QT_VERSION_CHECK(5,0,0))
		subdir.removeRecursively();
#else
		QStringList files = subdir.entryList(QDir::Files);
		foreach (const QString& file, files) {
			subdir.remove(file);
		}
		dir.rmdir(subdir_name);
#endif
	}

	return cachepath;
}

//-----------------------------------------------------------------------------

QString DocumentCache::createFileName()
{
	static time_t seed = 0;
	if (seed == 0) {
		seed = time(0);
		qsrand(seed);
	}

	QString filename;
	QDir dir(m_path);
	do {
		filename = QString("fw_%1").arg(qrand(), 6, 36, QLatin1Char('0'));
	} while (dir.exists(filename));

	return filename;
}

//-----------------------------------------------------------------------------
