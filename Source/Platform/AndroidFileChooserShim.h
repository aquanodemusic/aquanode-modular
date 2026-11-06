#pragma once

//==============================================================================
// Android: file pickers for the merged plugins' own panels.
//
// The plugins were written for the desktop, where a FileChooser hands back a
// File with a real path. Android's document picker hands back content://
// URIs instead: getResult() then gives a File that does not exist, and the
// plugin silently loads nothing. Rather than rewrite every plugin's file
// handling, the merged plugins' sources see this class in place of
// juce::FileChooser (PluginPrelude.h renames it for them, on Android only).
// It has the same interface and runs the real picker, but what a plugin's
// callback gets is a real file:
//
//   - opening a file: the picked document is copied into the app's own
//     storage (<app data>/Aquanode Modular/Imported/<name>) and getResult()
//     is that copy. Files imported earlier stay there, so a plugin that
//     browses the picked file's folder (prev / next preset) finds them.
//     The copy keeps the document's own name and extension (a .mp3 stays a
//     .mp3, whatever the picker's first pattern is); a document whose name
//     has no extension gets the one its first bytes call for.
//   - opening a folder: the folder's files (a few levels deep, up to a size
//     limit) are copied the same way, listed through the document provider
//     (aqp::android::listChildren); whatever the plugin writes into the copy
//     (an export) is written back into the picked folder afterwards.
//   - saving: getResult() is a file in a scratch folder; once the plugin's
//     callback has written it, its bytes go into the document the picker
//     created.
//
// getURLResult() / getURLResults() still give the picker's own URIs, for code
// that already handles them.
//==============================================================================
#include <JuceHeader.h>

// AQP_FILE_DEBUG 1: after every pick, a box says what happened (diagnostics)
#ifndef AQP_FILE_DEBUG
 #define AQP_FILE_DEBUG 0
#endif

#if JUCE_ANDROID
#include "AndroidDocuments.h"

namespace juce
{
class AqpFileChooser
{
public:
    AqpFileChooser (const String& title, const File& initial = {}, const String& patterns = {},
                    bool useNative = true, bool treatPackagesAsDirs = false, Component* parent = nullptr)
        : chooserTitle (title), startFile (initial), chooserPatterns (patterns), native (useNative),
          packages (treatPackagesAsDirs), parentComp (parent),
          extension (patterns.upToFirstOccurrenceOf (";", false, false).trim()
                              .fromLastOccurrenceOf ("*.", false, false).trim())
    {
    }

    ~AqpFileChooser()
    {
        if (session != nullptr)
            session->owner = nullptr;   // a result arriving now has no one to go to
    }

    void launchAsync (int flags, std::function<void (const AqpFileChooser&)> callback,
                      FilePreviewComponent* preview = nullptr)
    {
        // The picker runs in a session of its own that outlives this object
        // (as Aquanode Modular's own pickers live in their modules, not in a
        // window): Android delivers the result after its picker activity, and
        // whatever happened to the plugin's window meanwhile, the result still
        // arrives somewhere and is either handed on or reported.
        // Opening starts where the picker likes to start; saving keeps the
        // suggested name.
        const bool saving = (flags & FileBrowserComponent::saveMode) != 0;
        if (session != nullptr)
            session->owner = nullptr;
        session = std::make_shared<Session>();
        session->owner = this;
        session->chooser = std::make_unique<FileChooser> (chooserTitle, saving ? startFile : File(), aqp::android::pickerPatterns (chooserPatterns),
                                                          native, packages, parentComp);
        sessions().push_back (session);

        auto* s = session.get();
        s->chooser->launchAsync (flags, [s, flags, callback] (const FileChooser& fc)
        {
            const auto picked = fc.getURLResults();
            String trace = "picked " + String (picked.size()) + ": "
                         + (picked.isEmpty() ? String ("-") : URL::removeEscapeChars (picked[0].toString (false)));

            std::vector<WriteBack> pending;
            if (auto* self = s->owner)
            {
                self->urls = picked;
                self->files.clear();
                for (const auto& u : picked)
                {
                    const auto f = self->localFor (u, flags, pending);
                    self->files.add (f);
                    trace << "\nlocal: " << f.getFullPathName() << " (" << (f.existsAsFile() ? String (f.getSize()) : String ("none")) << ")";
                }

                // the callback may delete the chooser: nothing below touches it
                if (callback != nullptr)
                    callback (*self);
                trace << "\nplugin done";
                for (const auto& w : pending)
                    trace << "\n" << w.run();
            }
            else
            {
                trace << "\nthe plugin's window was gone before the picker returned";
            }

           #if AQP_FILE_DEBUG
            MessageManager::callAsync ([trace]
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "File (debug)", trace);
            });
           #endif

            // the session is done (not from inside its own chooser's callback)
            MessageManager::callAsync ([s]
            {
                auto& all = sessions();
                all.erase (std::remove_if (all.begin(), all.end(),
                                           [s] (const std::shared_ptr<Session>& p) { return p.get() == s; }),
                           all.end());
            });
        }, preview);
    }

    File getResult() const               { return files.isEmpty() ? File() : files.getReference (0); }
    Array<File> getResults() const       { return files; }
    URL getURLResult() const             { return urls.isEmpty() ? URL() : urls.getReference (0); }
    Array<URL> getURLResults() const     { return urls; }

    static File importFolder()
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("Aquanode Modular").getChildFile ("Imported");
    }

private:
    // what goes back to the picked document / folder after the callback
    struct WriteBack
    {
        URL target;
        File local;
        bool folder { false };
        Time since;

        String run() const
        {
            if (folder)
            {
                const auto tree = AndroidDocument::fromTree (target);
                if (tree.hasValue())
                    writeFolder (local, tree, since, 0);
                return tree.hasValue() ? "folder written back" : "folder: no access";
            }
            auto from = local;
            if (! from.existsAsFile())
            {
                // the plugin may have changed the extension: take what it wrote
                const auto written = local.getParentDirectory().findChildFiles (File::findFiles, false);
                if (written.isEmpty())
                {
                    report ("The plugin did not save anything to " + local.getFileName() + ".", target);
                    return "nothing saved by the plugin";
                }
                from = written.getFirst();
            }
            const auto problem = writeDocument (target, from);
            if (problem.isNotEmpty())
                report ("The file could not be written (" + problem + ").", target);
            return problem.isNotEmpty() ? "write-back: " + problem
                                        : "written back: " + String (from.getSize()) + " bytes";
        }
    };

    static String safeName (const String& n, const String& fallback)
    {
        const auto s = File::createLegalFileName (n.trim());
        return s.isNotEmpty() ? s : fallback;
    }

    // "Song.mp3" -> true, "1234" / "Take 2.5 final" -> false
    static bool hasExtension (const String& name)
    {
        if (! name.containsChar ('.')) return false;
        const auto ext = name.fromLastOccurrenceOf (".", false, false);
        return ext.length() >= 1 && ext.length() <= 5 && ext.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    }

    // The document's name, read from its URI (".../document/primary%3ADownload%2FKick.mp3"
    // -> "Kick.mp3"). Its own extension is kept whatever the chooser's patterns
    // are. A provider that names documents by number gives no extension: then
    // the content's first bytes choose one (when it has been read), else the
    // chooser's first pattern. The provider is not asked for the name: some
    // answer that query badly, and then nothing after it works.
    String nameFromUrl (const URL& u, const MemoryBlock* content = nullptr) const
    {
        auto tail = URL::removeEscapeChars (u.toString (false)).fromLastOccurrenceOf ("/", false, false);
        tail = tail.fromLastOccurrenceOf (":", false, false);
        auto name = safeName (tail, "Document");
        if (! hasExtension (name))
        {
            auto ext = content != nullptr ? aqp::android::sniffExtension (content->getData(), content->getSize()) : String();
            if (ext == "zip" && extension.isNotEmpty() && extension != "*" && extension != "zip")
                ext = extension;   // a zip-based format of the plugin's own (.phizmo, .aqua ...)
            if (ext.isEmpty() || ext == "state" || ext == "json" || ext == "xml")
                if (extension.isNotEmpty() && extension != "*")
                    ext = extension;
            if (ext.isNotEmpty())
                name << "." << ext;
        }
        return name;
    }

    // empty when it worked, otherwise what went wrong
    static String writeDocument (const URL& target, const File& from)
    {
        MemoryBlock mb;
        if (! from.loadFileAsData (mb) || mb.getSize() == 0)
            return "the plugin wrote nothing";
        // replacing what was there ("wt"): a shorter file over a longer one
        // must not keep the old one's tail
        const auto problem = aqp::android::replaceDocument (target, mb.getData(), mb.getSize());
        if (problem.isEmpty())
            return {};
        if (auto out = target.createOutputStream())
        {
            if (! out->write (mb.getData(), mb.getSize()))
                return "writing failed";
            out->flush();
            return {};
        }
        return problem;
    }
    static String writeDocument (const AndroidDocument& doc, const File& from)
    {
        MemoryBlock mb;
        if (! doc.hasValue() || ! from.loadFileAsData (mb))
            return "nothing to write";
        if (mb.getSize() > 0 && aqp::android::replaceDocument (doc.getUrl(), mb.getData(), mb.getSize()).isEmpty())
            return {};
        if (auto out = doc.createOutputStream())
        {
            out->write (mb.getData(), mb.getSize());
            out->flush();
            return {};
        }
        return "no write access";
    }

    // a problem the plugin could not have noticed itself, shown once
    static void report (const String& what, const URL& u)
    {
        MessageManager::callAsync ([what, u]
        {
            NativeMessageBox::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "File",
                what + "\n\n" + URL::removeEscapeChars (u.toString (false)));
        });
    }

    static String mimeFor (const File& f)
    {
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext == ".wav")                    return "audio/wav";
        if (ext == ".aif" || ext == ".aiff")  return "audio/aiff";
        if (ext == ".flac")                   return "audio/flac";
        if (ext == ".mid" || ext == ".midi")  return "audio/midi";
        if (ext == ".json")                   return "application/json";
        if (ext == ".txt")                    return "text/plain";
        return "application/octet-stream";
    }

    // files written into the local copy since `since`, into the picked folder
    // (the folder's contents come from the provider: aqp::android::listChildren)
    static void writeFolder (const File& local, const AndroidDocument& dir, Time since, int depth)
    {
        const auto children = aqp::android::listChildren (dir.getUrl());
        for (const auto& f : local.findChildFiles (File::findFilesAndDirectories, false))
        {
            AndroidDocument existing;
            for (const auto& child : children)
                if (child.name == f.getFileName()) { existing = AndroidDocument::fromDocument (child.url); break; }

            if (f.isDirectory())
            {
                if (depth >= 4) continue;
                auto sub = existing.hasValue() ? existing : dir.createChildDirectory (f.getFileName());
                if (sub.hasValue())
                    writeFolder (f, sub, since, depth + 1);
            }
            else if (f.getLastModificationTime() > since)
            {
                auto doc = existing.hasValue() ? existing
                                               : dir.createChildDocumentWithTypeAndName (mimeFor (f), f.getFileName());
                writeDocument (doc, f);
            }
        }
    }

    // the picked folder's files into `dest` (returns how many came across)
    static int copyFolderIn (const URL& dir, const File& dest, int depth, int64& budget, String& problem)
    {
        dest.createDirectory();
        int copied = 0;
        String listProblem;
        const auto children = aqp::android::listChildren (dir, &listProblem);
        if (children.empty() && listProblem.isNotEmpty() && problem.isEmpty())
            problem = listProblem;
        for (const auto& child : children)
        {
            const auto name = safeName (child.name, {});
            if (name.isEmpty())
                continue;
            if (child.isFolder)
            {
                if (depth < 4 && ! name.endsWithIgnoreCase (".vst3") && ! name.endsWithIgnoreCase (".component"))
                    copied += copyFolderIn (child.url, dest.getChildFile (name), depth + 1, budget, problem);
                continue;
            }
            if (child.size > budget)
                continue;
            // programs and manuals are never what a plugin reads from a folder (a
            // synth's folder picked whole for its waveforms brings them along)
            if (name.endsWithIgnoreCase (".dll") || name.endsWithIgnoreCase (".exe") || name.endsWithIgnoreCase (".vst3")
                || name.endsWithIgnoreCase (".pdf") || name.endsWithIgnoreCase (".so") || name.endsWithIgnoreCase (".dylib"))
                continue;
            std::unique_ptr<InputStream> in;
            if (auto doc = AndroidDocument::fromDocument (child.url); doc.hasValue())
                in = doc.createInputStream();
            if (in == nullptr)
                in = child.url.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress));
            if (in != nullptr)
            {
                MemoryBlock mb;
                in->readIntoMemoryBlock (mb);
                budget -= (int64) mb.getSize();
                if (dest.getChildFile (name).replaceWithData (mb.getData(), mb.getSize()))
                    ++copied;
            }
            else if (problem.isEmpty())
            {
                problem = name + " could not be read";
            }
            if (budget <= 0)
                return copied;
        }
        return copied;
    }

    File localFor (const URL& u, int flags, std::vector<WriteBack>& pending)
    {
        // Only a file:// URL is a file this app may use by its path. On Android
        // URL::isLocalFile() also says yes to a content:// document it can map
        // to a path in shared storage (/storage/emulated/0/Download/...), but
        // the app may not read or write that path itself - the plugin then
        // loads nothing and saves nothing. So a content:// document is always
        // gone through as a document.
        if (u.getScheme() == "file")
            return u.getLocalFile();

        const bool saving = (flags & FileBrowserComponent::saveMode) != 0;
        const bool folder = (flags & FileBrowserComponent::canSelectDirectories) != 0
                            && (flags & FileBrowserComponent::canSelectFiles) == 0;

        if (folder)
        {
            const auto tree = AndroidDocument::fromTree (u);
            if (! tree.hasValue())
                return {};
            const auto local = importFolder().getChildFile ("Folders")
                                   .getChildFile (safeName (URL::removeEscapeChars (u.toString (false))
                                                                .fromLastOccurrenceOf ("/", false, false)
                                                                .fromLastOccurrenceOf (":", false, false), "Folder"));
            local.deleteRecursively();
            int64 budget = (int64) 256 * 1024 * 1024;
            String problem;
            // (an empty folder is fine: a plugin may export into it)
            if (copyFolderIn (tree.getUrl(), local, 0, budget, problem) == 0 && problem.isNotEmpty())
                report ("The folder could not be read (" + problem + ").", u);
            // what the plugin writes from now on goes back (not the copies just made)
            pending.push_back ({ u, local, true, Time::getCurrentTime() });
            return local;
        }

        if (saving)
        {
            const auto dir = File::getSpecialLocation (File::tempDirectory)
                                 .getChildFile ("aqp_save").getNonexistentChildFile ("s", {}, false);
            dir.createDirectory();
            const auto local = dir.getChildFile (nameFromUrl (u));
            pending.push_back ({ u, local, false, {} });
            return local;
        }

        // the document's bytes: through the document API, or failing that the
        // content resolver directly
        std::unique_ptr<InputStream> in;
        if (auto doc = AndroidDocument::fromDocument (u); doc.hasValue())
            in = doc.createInputStream();
        if (in == nullptr)
            in = u.createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress));
        if (in == nullptr)
        {
            report ("The file could not be opened.", u);
            return {};
        }
        MemoryBlock mb;
        in->readIntoMemoryBlock (mb);
        if (mb.getSize() == 0)
        {
            report ("The file is empty.", u);
            return {};
        }
        importFolder().createDirectory();
        const auto local = importFolder().getChildFile (nameFromUrl (u, &mb));
        if (! local.replaceWithData (mb.getData(), mb.getSize()))
        {
            report ("The file could not be copied into the app's storage.", u);
            return {};
        }
        return local;
    }

    struct Session
    {
        std::unique_ptr<FileChooser> chooser;
        AqpFileChooser* owner { nullptr };
    };
    static std::vector<std::shared_ptr<Session>>& sessions()
    {
        static std::vector<std::shared_ptr<Session>> all;
        return all;
    }

    String chooserTitle;
    File startFile;
    String chooserPatterns;
    bool native { true }, packages { false };
    Component* parentComp { nullptr };
    std::shared_ptr<Session> session;
    String extension;   // of the chooser's first pattern ("phizmo"), for documents with no name
    Array<File> files;
    Array<URL> urls;
};
} // namespace juce

#endif
