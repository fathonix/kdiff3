/*
 * KDiff3 - Text Diff And Merge Tool
 *
 * SPDX-FileCopyrightText: 2002-2011 Joachim Eibl, joachim.eibl at gmx.de
 * SPDX-FileCopyrightText: 2018-2020 Michael Reeves reeves.87@gmail.com
 * SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "fileaccess.h"
#include "cvsignorelist.h"
#include "common.h"
#include "FileAccessJobHandler.h"
#include "Logging.h"
#include "progress.h"
#include "Utils.h"

#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#include <vector>

#include <QDir>
#include <QFile>
#include <QtMath>
#include <QRegExp>
#include <QTemporaryFile>

#include <KLocalizedString>

FileAccess::FileAccess(const QString& name, bool bWantToWrite)
{
    setFile(name, bWantToWrite);
}

FileAccess::FileAccess(const QUrl& name, bool bWantToWrite)
{
    setFile(name, bWantToWrite);
}

void FileAccess::reset()
{
    *this = FileAccess();
}

/*
    Needed only during directory listing right now.
*/
void FileAccess::setFile(FileAccess* pParent, const QFileInfo& fi)
{
    Q_ASSERT(pParent != this);
    reset();

    m_fileInfo = fi;
    m_url = QUrl::fromLocalFile(m_fileInfo.absoluteFilePath());

    m_pParent = pParent;
    loadData();
}

void FileAccess::setFile(const QString& name, bool bWantToWrite)
{
    if(name.isEmpty())
        return;

    QUrl url = QUrl::fromUserInput(name, QString(), QUrl::AssumeLocalFile);
    setFile(url, bWantToWrite);
}

void FileAccess::setFile(const QUrl& url, bool bWantToWrite)
{
    reset();
    Q_ASSERT(parent() == nullptr || url != parent()->url());

    m_url = url;

    if(isLocal()) // Invalid urls are treated as local files.
    {
        m_fileInfo.setFile(m_url.toLocalFile());
        m_pParent = nullptr;

        loadData();
    }
    else
    {
        m_name = m_url.fileName();
        FileAccessJobHandler jh(this);            // A friend, which writes to the parameters of this class!
        if(jh.stat(2 /*all details*/, bWantToWrite))
            m_bValidData = true; // After running stat() the variables are initialised
                                // and valid even if the file doesn't exist and the stat
                                // query failed.
    }
}

void FileAccess::loadData()
{
    m_fileInfo.setCaching(true);

    if(parent() == nullptr)
        m_baseDir.setPath(m_fileInfo.absoluteFilePath());
    else
        m_baseDir = m_pParent->m_baseDir;

    //convert to absolute path that doesn't depend on the current directory.
    m_fileInfo.makeAbsolute();
    m_bSymLink = m_fileInfo.isSymLink();

    m_bFile = m_fileInfo.isFile();
    m_bDir = m_fileInfo.isDir();
    m_bExists = m_fileInfo.exists();
    m_size = m_fileInfo.size();
    m_modificationTime = m_fileInfo.lastModified();
    m_bHidden = m_fileInfo.isHidden();

    m_bWritable = m_fileInfo.isWritable();
    m_bReadable = m_fileInfo.isReadable();
    m_bExecutable = m_fileInfo.isExecutable();

    m_name = m_fileInfo.fileName();
    if(isLocal() && m_name.isEmpty())
    {
        m_name = m_fileInfo.absoluteDir().dirName();
    }

    if(isLocal() && m_bSymLink)
    {
        m_linkTarget = m_fileInfo.symLinkTarget();
#ifndef Q_OS_WIN
        // Unfortunately Qt5 symLinkTarget/readLink always returns an absolute path, even if the link is relative
        char* s = (char*)malloc(PATH_MAX + 1);
        ssize_t len = readlink(QFile::encodeName(absoluteFilePath()).constData(), s, PATH_MAX);
        if(len > 0)
        {
            s[len] = '\0';
            m_linkTarget = QFile::decodeName(s);
        }
        free(s);
#endif
    }

    realFile = QSharedPointer<QFile>::create(absoluteFilePath());
    m_bValidData = true;
}

void FileAccess::addPath(const QString& txt, bool reinit)
{
    if(!isLocal())
    {
        QUrl url = m_url.adjusted(QUrl::StripTrailingSlash);
        url.setPath(url.path() + '/' + txt);
        m_url = url;

        if(reinit)
            setFile(url); // reinitialize
    }
    else
    {
        QString slash = (txt.isEmpty() || txt[0] == '/') ? QLatin1String("") : QLatin1String("/");
        setFile(absoluteFilePath() + slash + txt);
    }
}

#ifndef AUTOTEST
/*     Filetype:
       S_IFMT     0170000   bitmask for the file type bitfields
       S_IFSOCK   0140000   socket
       S_IFLNK    0120000   symbolic link
       S_IFREG    0100000   regular file
       S_IFBLK    0060000   block device
       S_IFDIR    0040000   directory
       S_IFCHR    0020000   character device
       S_IFIFO    0010000   fifo
       S_ISUID    0004000   set UID bit
       S_ISGID    0002000   set GID bit (see below)
       S_ISVTX    0001000   sticky bit (see below)

       Access:
       S_IRWXU    00700     mask for file owner permissions
       S_IRUSR    00400     owner has read permission
       S_IWUSR    00200     owner has write permission
       S_IXUSR    00100     owner has execute permission
       S_IRWXG    00070     mask for group permissions
       S_IRGRP    00040     group has read permission
       S_IWGRP    00020     group has write permission
       S_IXGRP    00010     group has execute permission
       S_IRWXO    00007     mask for permissions for others (not in group)
       S_IROTH    00004     others have read permission
       S_IWOTH    00002     others have write permission
       S_IXOTH    00001     others have execute permission
*/
void FileAccess::setFromUdsEntry(const KIO::UDSEntry& e, FileAccess *parent)
{
    long acc = 0;
    long fileType = 0;
    const QVector<uint> fields = e.fields();
    QString filePath;

    Q_ASSERT(this != parent);
    m_pParent = parent;
    
    for(const uint fieldId: fields)
    {
        switch(fieldId)
        {
            case KIO::UDSEntry::UDS_SIZE:
                m_size = e.numberValue(fieldId);
                break;
            case KIO::UDSEntry::UDS_NAME:
                filePath = e.stringValue(fieldId);
                qCDebug(kdiffFileAccess) << "filePath = " << filePath;
                break; // During listDir the relative path is given here.
            case KIO::UDSEntry::UDS_MODIFICATION_TIME:
                m_modificationTime = QDateTime::fromMSecsSinceEpoch(e.numberValue(fieldId));
                break;
            case KIO::UDSEntry::UDS_LINK_DEST:
                m_linkTarget = e.stringValue(fieldId);
                break;
            case KIO::UDSEntry::UDS_ACCESS:
#ifndef Q_OS_WIN
                acc = e.numberValue(fieldId);
                m_bReadable = (acc & S_IRUSR) != 0;
                m_bWritable = (acc & S_IWUSR) != 0;
                m_bExecutable = (acc & S_IXUSR) != 0;
#endif
                break;
            case KIO::UDSEntry::UDS_FILE_TYPE:
                /*
                    According to KIO docs UDS_LINK_DEST not S_ISLNK should be used to determine if the url is a symlink.
                    UDS_FILE_TYPE is explicitly stated to be the type of the linked file not the link itself.
                */

                m_bSymLink = e.isLink();
                if(!m_bSymLink)
                {
                    fileType = e.numberValue(fieldId);
                    m_bDir = (fileType & QT_STAT_MASK) == QT_STAT_DIR;
                    m_bFile = (fileType & QT_STAT_MASK) == QT_STAT_REG;
                    m_bExists = fileType != 0;
                }
                else
                {
                    m_bDir = false;
                    m_bFile = false;
                    m_bExists = true;
                }
                break;
            case KIO::UDSEntry::UDS_URL:
                m_url = QUrl(e.stringValue(fieldId));
                qCDebug(kdiffFileAccess) << "Url = " << m_url;
                break;
            case KIO::UDSEntry::UDS_DISPLAY_NAME:
                mDisplayName = e.stringValue(fieldId);
                break;
            case KIO::UDSEntry::UDS_LOCAL_PATH:
                mPhysicalPath = e.stringValue(fieldId);
                break;
            case KIO::UDSEntry::UDS_MIME_TYPE:
            case KIO::UDSEntry::UDS_GUESSED_MIME_TYPE:
            case KIO::UDSEntry::UDS_XML_PROPERTIES:
            default:
                break;
        }
    }
    //If this happens its a protocol bug. We can't do anything without knowing what file/folder we are working with.
    if(Q_UNLIKELY(filePath.isEmpty()))
    {
        /*
            Invalid entry we don't know the fileName because KIO didn't tell us.
            This is a bug if it happens and should be logged. However it is a recoverable error.
        */
        qCCritical(kdiffFileAccess) << i18n("Unable to determine full url. No file path/name specified.");
        return;
    }

    m_fileInfo = QFileInfo(filePath);
    m_fileInfo.setCaching(true);
    //Seems to be the norm for fish and possibly other prototcol handlers.
    if(m_url.isEmpty())
    {
        qCInfo(kdiffFileAccess) << "Url not received from KIO.";
        if(Q_UNLIKELY(parent == nullptr))
        {
            /*
             Invalid entry we don't know the full url because KIO didn't tell us and there is no parent
             node supplied.
             This is a bug if it happens and should be logged. However it is a recoverable error.
            */
            qCCritical(kdiffFileAccess) << i18n("Unable to determine full url. No parent specified.");
            return;
        }
        /*
            Don't trust QUrl::resolved it doesn't always do what kdiff3 wants.
        */
        m_url = parent->url();
        addPath(filePath, false);

        qCDebug(kdiffFileAccess) << "Computed url is: " << m_url;
        //Verify that the scheme doesn't change.
        Q_ASSERT(m_url.scheme() == parent->url().scheme());
    }

    m_name = m_fileInfo.fileName();

    if(isLocal())
    {
        if(m_name.isEmpty())
            m_name = m_fileInfo.absoluteDir().dirName();
        
        m_bExists = m_fileInfo.exists();

        //insure modification time is initialized if it wasn't already.
        if(m_modificationTime == QDateTime::fromMSecsSinceEpoch(0))
            m_modificationTime = m_fileInfo.lastModified();
    }
        
    m_bValidData = true;
    m_bSymLink = !m_linkTarget.isEmpty();

#ifndef Q_OS_WIN
    m_bHidden = m_name[0] == '.';
#endif
}
#endif

bool FileAccess::isValid() const
{
    return m_bValidData;
}

bool FileAccess::isNormal() const
{
    /*
        Speed is important here isNormal is called for every file during directory
        comparison. It can therefor have great impact on overall performance.

        We also need to insure that we don't keep looking indefinitely when following
        links that point to links. Therefore we hard cap at 15 such links in a chain
        and make sure we don't cycle back to something we already saw.
    */
    if(!mVisited && mDepth < 15 && isLocal() && isSymLink())
    {
        FileAccess target(m_linkTarget);

        mVisited = true;
        ++mDepth;
        /*
            Catch local links to special files. '/dev' has many of these.
        */
        bool result = target.isSymLink() || target.isNormal();
        // mVisited has done its job and should be reset here.
        mVisited = false;
        --mDepth;

        return result;
    }

    mVisited = false;
    mDepth = 0;

    return !exists() || isFile() || isDir() || isSymLink();
}

bool FileAccess::isFile() const
{
    if(!isLocal())
        return m_bFile;
    else
        return m_fileInfo.isFile();
}

bool FileAccess::isDir() const
{
    if(!isLocal())
        return m_bDir;
    else
        return m_fileInfo.isDir();
}

bool FileAccess::isSymLink() const
{
    if(!isLocal())
        return m_bSymLink;
    else
        return m_fileInfo.isSymLink();
}

bool FileAccess::exists() const
{
    if(!isLocal())
        return m_bExists;
    else//Thank you git for being different.
        return m_fileInfo.exists() && absoluteFilePath() != "/dev/null";
}

qint64 FileAccess::size() const
{
    if(!isLocal())
        return m_size;
    else
        return m_fileInfo.size();
}

QUrl FileAccess::url() const
{
    return m_url;
}

/*
    FileAccess::isLocal() should return whether or not the m_url contains what KDiff3 considers
    a local i.e. non-KIO path. This is not the necessarily same as what QUrl::isLocalFile thinks.
*/
bool FileAccess::isLocal() const
{
    return m_url.isLocalFile() || !m_url.isValid() || m_url.scheme().isEmpty();
}

bool FileAccess::isReadable() const
{
    //This can be very slow in some network setups so use cached value
    if(!isLocal())
        return m_bReadable;
    else
        return m_fileInfo.isReadable();
}

bool FileAccess::isWritable() const
{
    //This can be very slow in some network setups so use cached value
    if(!isLocal())
        return m_bWritable;
    else
        return m_fileInfo.isWritable();
}

bool FileAccess::isExecutable() const
{
    //This can be very slow in some network setups so use cached value
    if(!isLocal())
        return m_bExecutable;
    else
        return m_fileInfo.isExecutable();
}

bool FileAccess::isHidden() const
{
    if(!(isLocal()))
        return m_bHidden;
    else
        return m_fileInfo.isHidden();
}

QString FileAccess::readLink() const
{
    return m_linkTarget;
}

QString FileAccess::absoluteFilePath() const
{
    if(!isLocal())
        return m_url.url(); // return complete url

    return m_fileInfo.absoluteFilePath();
} // Full abs path

// Just the name-part of the path, without parent directories
QString FileAccess::fileName(bool needTmp) const
{
    if(!isLocal())
        return (needTmp) ? m_localCopy : m_name;
    else
        return m_name;
}

QString FileAccess::fileRelPath() const
{
#ifndef AUTOTEST
    Q_ASSERT(m_pParent == nullptr || m_baseDir == m_pParent->m_baseDir);
#endif
    QString path;

    if(isLocal())
    {
        path = m_baseDir.relativeFilePath(m_fileInfo.absoluteFilePath());

        return path;
    }
    else
    {
        //Stop right before the root directory
        if(parent() == nullptr) return QString();

        const FileAccess *curEntry = this;
        path = fileName();
        //Avoid recursing to FileAccess::fileRelPath or we can get very large stacks.
        curEntry = curEntry->parent();
        while(curEntry != nullptr)
        {
            if(curEntry->parent())
                path.prepend(curEntry->fileName() + "/");
            curEntry= curEntry->parent();
        }
        return path;
    }    
}

FileAccess* FileAccess::parent() const
{
    Q_ASSERT(m_pParent != this);
    return m_pParent;
}

//Workaround for QUrl::toDisplayString/QUrl::toString behavior that does not fit KDiff3's expectations
QString FileAccess::prettyAbsPath() const
{
    return isLocal() ? absoluteFilePath() : m_url.toDisplayString();
}

QDateTime FileAccess::lastModified() const
{
    Q_ASSERT(!m_modificationTime.isNull());
    return m_modificationTime;
}

bool FileAccess::interruptableReadFile(void* pDestBuffer, qint64 maxLength)
{
    ProgressProxy pp;
    const qint64 maxChunkSize = 100000;
    qint64 i = 0;
    pp.setMaxNofSteps(maxLength / maxChunkSize + 1);
    while(i < maxLength)
    {
        qint64 nextLength = std::min(maxLength - i, maxChunkSize);
        qint64 reallyRead = read((char*)pDestBuffer + i, nextLength);
        if(reallyRead != nextLength)
        {
            setStatusText(i18n("Failed to read file: %1", absoluteFilePath()));
            return false;
        }
        i += reallyRead;

        pp.setCurrent(qFloor(double(i) / maxLength * 100));
        if(pp.wasCancelled())
            return false;
    }
    return true;
}

bool FileAccess::readFile(void* pDestBuffer, qint64 maxLength)
{
    bool success = false;
    //Avoid hang on linux for special files.
    if(!isNormal())
        return true;

    if(isLocal() || !m_localCopy.isEmpty())
    {
        if(open(QIODevice::ReadOnly))//krazy:exclude=syscalls
        {
            success = interruptableReadFile(pDestBuffer, maxLength); // maxLength == f.read( (char*)pDestBuffer, maxLength )
            close();
        }
    }
    else
    {
        FileAccessJobHandler jh(this);
        success = jh.get(pDestBuffer, maxLength);
    }

    close();
    Q_ASSERT(!realFile->isOpen() && !tmpFile->isOpen());
    return success;
}

bool FileAccess::writeFile(const void* pSrcBuffer, qint64 length)
{
    ProgressProxy pp;
    if(isLocal())
    {
        if(realFile->open(QIODevice::WriteOnly))
        {
            const qint64 maxChunkSize = 100000;
            pp.setMaxNofSteps(length / maxChunkSize + 1);
            qint64 i = 0;
            while(i < length)
            {
                qint64 nextLength = std::min(length - i, maxChunkSize);
                qint64 reallyWritten = realFile->write((char*)pSrcBuffer + i, nextLength);
                if(reallyWritten != nextLength)
                {
                    realFile->close();
                    return false;
                }
                i += reallyWritten;

                pp.step();
                if(pp.wasCancelled())
                {
                    realFile->close();
                    return false;
                }
            }

            if(isExecutable()) // value is true if the old file was executable
            {
                // Preserve attributes
                realFile->setPermissions(realFile->permissions() | QFile::ExeUser);
            }

            realFile->close();
            return true;
        }
    }
    else
    {
        FileAccessJobHandler jh(this);
        bool success = jh.put(pSrcBuffer, length, true /*overwrite*/);
        close();

        Q_ASSERT(!realFile->isOpen() && !tmpFile->isOpen());

        return success;
    }
    close();
    Q_ASSERT(!realFile->isOpen() && !tmpFile->isOpen());
    return false;
}

bool FileAccess::copyFile(const QString& dest)
{
    FileAccessJobHandler jh(this);
    return jh.copyFile(dest); // Handles local and remote copying.
}

bool FileAccess::rename(const FileAccess& dest)
{
    FileAccessJobHandler jh(this);
    return jh.rename(dest);
}

bool FileAccess::removeFile()
{
    if(isLocal())
    {
        return QDir().remove(absoluteFilePath());
    }
    else
    {
        FileAccessJobHandler jh(this);
        return jh.removeFile(url());
    }
}

bool FileAccess::listDir(t_DirectoryList* pDirList, bool bRecursive, bool bFindHidden,
                         const QString& filePattern, const QString& fileAntiPattern, const QString& dirAntiPattern,
                         bool bFollowDirLinks, bool bUseCvsIgnore)
{
    FileAccessJobHandler jh(this);
    return jh.listDir(pDirList, bRecursive, bFindHidden, filePattern, fileAntiPattern,
                      dirAntiPattern, bFollowDirLinks, bUseCvsIgnore);
}

QString FileAccess::getTempName() const
{
    if(mPhysicalPath.isEmpty())
        return m_localCopy;
    else
        return mPhysicalPath;
}

const QString& FileAccess::errorString() const
{
    return getStatusText();
}

bool FileAccess::open(const QFile::OpenMode flags)
{
    bool result;
    result = createLocalCopy();
    if(!result)
    {
        setStatusText(i18n("Creating temp copy of %1 failed.", absoluteFilePath()));
        return result;
    }

    if(m_localCopy.isEmpty() && realFile != nullptr)
    {
        bool r = realFile->open(flags);

        setStatusText(i18n("Opening %1 failed. %2", absoluteFilePath(), realFile->errorString()));
        return r;
    }

    bool r = tmpFile->open();
    setStatusText(i18n("Opening %1 failed. %2", tmpFile->fileName(), tmpFile->errorString()));
    return r;
}

qint64 FileAccess::read(char* data, const qint64 maxlen)
{
    if(!isNormal())
    {
        //This is not an error special files should be skipped
        setStatusText(QString());
        return 0;
    }

    qint64 len = 0;
    if(m_localCopy.isEmpty() && realFile != nullptr)
    {
        len = realFile->read(data, maxlen);
        if(len != maxlen)
        {
            setStatusText(i18n("Error reading from %1. %2", absoluteFilePath(), realFile->errorString()));
        }
    }
    else
    {
        len = tmpFile->read(data, maxlen);
        if(len != maxlen)
        {
            setStatusText(i18n("Error reading from %1. %2", absoluteFilePath(), tmpFile->errorString()));
        }
    }

    return len;
}

void FileAccess::close()
{
    if(m_localCopy.isEmpty() && realFile != nullptr)
    {
        realFile->close();
    }

    tmpFile->close();
}

bool FileAccess::createLocalCopy()
{
    if(isLocal() || !m_localCopy.isEmpty() || !mPhysicalPath.isEmpty())
        return true;

    tmpFile->setAutoRemove(true);
    tmpFile->open();
    tmpFile->close();
    m_localCopy = tmpFile->fileName();

    return copyFile(tmpFile->fileName());
}
//static tempfile Generator
void FileAccess::createTempFile(QTemporaryFile& tmpFile)
{
    tmpFile.setAutoRemove(true);
    tmpFile.open();
    tmpFile.close();
}

bool FileAccess::makeDir(const QString& dirName)
{
    FileAccessJobHandler fh(nullptr);
    return fh.mkDir(dirName);
}

bool FileAccess::removeDir(const QString& dirName)
{
    FileAccessJobHandler fh(nullptr);
    return fh.rmDir(dirName);
}

bool FileAccess::symLink(const QString& linkTarget, const QString& linkLocation)
{
    if(linkTarget.isEmpty() || linkLocation.isEmpty())
        return false;
    return QFile::link(linkTarget, linkLocation);
    //FileAccessJobHandler fh(0);
    //return fh.symLink( linkTarget, linkLocation );
}

bool FileAccess::exists(const QString& name)
{
    FileAccess fa(name);
    return fa.exists();
}

// If the size couldn't be determined by stat() then the file is copied to a local temp file.
qint64 FileAccess::sizeForReading()
{
    if(!isLocal() && m_size == 0 && mPhysicalPath.isEmpty())
    {
        // Size couldn't be determined. Copy the file to a local temp place.
        createLocalCopy();
        QString localCopy = tmpFile->fileName();
        bool bSuccess = copyFile(localCopy);
        if(bSuccess)
        {
            QFileInfo fi(localCopy);
            m_size = fi.size();
            m_localCopy = localCopy;
            return m_size;
        }
        else
        {
            return 0;
        }
    }
    else
        return size();
}

const QString& FileAccess::getStatusText() const
{
    return m_statusText;
}

void FileAccess::setStatusText(const QString& s)
{
    m_statusText = s;
}

QString FileAccess::cleanPath(const QString& path) // static
{
    /*
        Tell Qt to treat the supplied path as user input otherwise it will not make usefull decisions
        about how to convert from the possibly local or remote "path" string to QUrl.
    */
    QUrl url = QUrl::fromUserInput(path, QString(), QUrl::AssumeLocalFile);

    if( FileAccess::isLocal(url) )
    {
        return QDir::cleanPath(path);
    }
    else
    {
        return path;
    }
}

bool FileAccess::createBackup(const QString& bakExtension)
{
    if(exists())
    {
        // First rename the existing file to the bak-file. If a bak-file file exists, delete that.
        QString bakName = absoluteFilePath() + bakExtension;
        FileAccess bakFile(bakName, true /*bWantToWrite*/);
        if(bakFile.exists())
        {
            bool bSuccess = bakFile.removeFile();
            if(!bSuccess)
            {
                setStatusText(i18n("While trying to make a backup, deleting an older backup failed.\nFilename: %1", bakName));
                return false;
            }
        }
        bool bSuccess = rename(bakFile); // krazy:exclude=syscalls
        if(!bSuccess)
        {
            setStatusText(i18n("While trying to make a backup, renaming failed.\nFilenames: %1 -> %2",
                               absoluteFilePath(), bakName));
            return false;
        }
    }
    return true;
}

void FileAccess::doError()
{
    m_bValidData = true;
    m_bExists = false;
}

void FileAccess::filterList(t_DirectoryList* pDirList, const QString& filePattern,
                            const QString& fileAntiPattern, const QString& dirAntiPattern,
                            const bool bUseCvsIgnore)
{
    CvsIgnoreList cvsIgnoreList;
    if(bUseCvsIgnore)
    {
        #ifndef AUTOTEST
        cvsIgnoreList.init(*this, pDirList);
        #endif
    }
    //TODO: Ask os for this information don't hard code it.
#if defined(Q_OS_WIN)
    bool bCaseSensitive = false;
#else
    bool bCaseSensitive = true;
#endif

    // Now remove all entries that should be ignored:
    t_DirectoryList::iterator i;
    for(i = pDirList->begin(); i != pDirList->end();)
    {
        t_DirectoryList::iterator i2 = i;
        ++i2;
        QString fileName = i->fileName();

        if((i->isFile() &&
            (!Utils::wildcardMultiMatch(filePattern, fileName, bCaseSensitive) ||
             Utils::wildcardMultiMatch(fileAntiPattern, fileName, bCaseSensitive))) ||
           (i->isDir() && Utils::wildcardMultiMatch(dirAntiPattern, fileName, bCaseSensitive)) ||
           (bUseCvsIgnore && cvsIgnoreList.matches(fileName, bCaseSensitive)))
        {
            // Remove it
            pDirList->erase(i);
            i = i2;
        }
        else
        {
            ++i;
        }
    }
}

//#include "fileaccess.moc"
