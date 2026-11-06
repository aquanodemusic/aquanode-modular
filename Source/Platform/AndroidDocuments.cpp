// JUCE's JNI helpers (getEnv, LocalRef, javaString ...) come with juce_core
// only when this is defined before the first JUCE header of the file.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1
#include <JuceHeader.h>
#include "AndroidDocuments.h"

#if JUCE_ANDROID
namespace aqp::android
{
    using namespace juce;

    namespace
    {
        // Every class and method is looked up here, when it is needed, and
        // every call checked: a provider that refuses something leaves a Java
        // exception behind, which is cleared and reported instead of being
        // left for the next JNI call to trip over.
        bool failed (JNIEnv* env)
        {
            if (! env->ExceptionCheck()) return false;
            env->ExceptionClear();
            return true;
        }

        // read (dest, max) -> bytes read (0 at the end, -1 on an error)
        using Reader = std::function<int (void*, int)>;

        String writeWithMode (const URL& document, const Reader& read, const char* mode)
        {
            auto* env = getEnv();
            if (env == nullptr) return "no Java environment";

            const auto context = getAppContext();
            if (context == nullptr) return "no app context";
            LocalRef<jclass> contextClass (env->GetObjectClass (context.get()));
            const auto getResolver = env->GetMethodID (contextClass.get(), "getContentResolver", "()Landroid/content/ContentResolver;");
            if (failed (env) || getResolver == nullptr) return "no content resolver";
            LocalRef<jobject> resolver (env->CallObjectMethod (context.get(), getResolver));
            if (failed (env) || resolver == nullptr) return "no content resolver";

            LocalRef<jclass> uriClass (env->FindClass ("android/net/Uri"));
            if (failed (env) || uriClass == nullptr) return "no Uri class";
            const auto parse = env->GetStaticMethodID (uriClass.get(), "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
            if (failed (env) || parse == nullptr) return "no Uri.parse";
            LocalRef<jobject> uri (env->CallStaticObjectMethod (uriClass.get(), parse, javaString (document.toString (true)).get()));
            if (failed (env) || uri == nullptr) return "a bad document address";

            LocalRef<jclass> resolverClass (env->GetObjectClass (resolver.get()));
            const auto open = env->GetMethodID (resolverClass.get(), "openOutputStream",
                                                "(Landroid/net/Uri;Ljava/lang/String;)Ljava/io/OutputStream;");
            if (failed (env) || open == nullptr) return "no openOutputStream";
            LocalRef<jobject> stream (env->CallObjectMethod (resolver.get(), open, uri.get(), javaString (mode).get()));
            if (failed (env) || stream == nullptr) return String ("the provider would not open it (\"") + mode + "\")";

            LocalRef<jclass> streamClass (env->GetObjectClass (stream.get()));
            const auto write = env->GetMethodID (streamClass.get(), "write", "([BII)V");
            const auto flush = env->GetMethodID (streamClass.get(), "flush", "()V");
            const auto close = env->GetMethodID (streamClass.get(), "close", "()V");
            if (failed (env) || write == nullptr || flush == nullptr || close == nullptr) return "no stream methods";

            String problem;
            constexpr jsize chunk = 64 * 1024;
            LocalRef<jbyteArray> buffer (env->NewByteArray (chunk));
            if (failed (env) || buffer == nullptr)
                problem = "out of memory";
            HeapBlock<char> bytes ((size_t) chunk);
            while (problem.isEmpty())
            {
                const int n = read (bytes.get(), (int) chunk);
                if (n < 0) { problem = "reading failed"; break; }
                if (n == 0) break;
                env->SetByteArrayRegion (buffer.get(), 0, (jsize) n, reinterpret_cast<const jbyte*> (bytes.get()));
                env->CallVoidMethod (stream.get(), write, buffer.get(), (jint) 0, (jint) n);
                if (failed (env)) problem = "writing failed";
            }
            if (problem.isEmpty())
            {
                env->CallVoidMethod (stream.get(), flush);
                if (failed (env)) problem = "writing failed";
            }
            env->CallVoidMethod (stream.get(), close);
            if (failed (env) && problem.isEmpty()) problem = "the file could not be finished";
            return problem;
        }
    }

    std::vector<ChildDocument> listChildren (const URL& folder, String* problem)
    {
        std::vector<ChildDocument> out;
        auto fail = [&out, problem] (const char* what) { if (problem != nullptr) *problem = what; return out; };

        auto* env = getEnv();
        if (env == nullptr) return fail ("no Java environment");
        const auto context = getAppContext();
        if (context == nullptr) return fail ("no app context");
        LocalRef<jclass> contextClass (env->GetObjectClass (context.get()));
        const auto getResolver = env->GetMethodID (contextClass.get(), "getContentResolver", "()Landroid/content/ContentResolver;");
        if (failed (env) || getResolver == nullptr) return fail ("no content resolver");
        LocalRef<jobject> resolver (env->CallObjectMethod (context.get(), getResolver));
        if (failed (env) || resolver == nullptr) return fail ("no content resolver");

        LocalRef<jclass> uriClass (env->FindClass ("android/net/Uri"));
        if (failed (env) || uriClass == nullptr) return fail ("no Uri class");
        const auto parse = env->GetStaticMethodID (uriClass.get(), "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        const auto uriToString = env->GetMethodID (uriClass.get(), "toString", "()Ljava/lang/String;");
        if (failed (env) || parse == nullptr || uriToString == nullptr) return fail ("no Uri methods");
        const auto address = folder.toString (true);
        LocalRef<jobject> uri (env->CallStaticObjectMethod (uriClass.get(), parse, javaString (address).get()));
        if (failed (env) || uri == nullptr) return fail ("a bad folder address");

        LocalRef<jclass> contract (env->FindClass ("android/provider/DocumentsContract"));
        if (failed (env) || contract == nullptr) return fail ("no DocumentsContract");
        const auto getTreeId = env->GetStaticMethodID (contract.get(), "getTreeDocumentId", "(Landroid/net/Uri;)Ljava/lang/String;");
        const auto getDocId = env->GetStaticMethodID (contract.get(), "getDocumentId", "(Landroid/net/Uri;)Ljava/lang/String;");
        const auto childrenOf = env->GetStaticMethodID (contract.get(), "buildChildDocumentsUriUsingTree",
                                                        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/net/Uri;");
        const auto documentOf = env->GetStaticMethodID (contract.get(), "buildDocumentUriUsingTree",
                                                        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/net/Uri;");
        if (failed (env) || getTreeId == nullptr || getDocId == nullptr || childrenOf == nullptr || documentOf == nullptr)
            return fail ("no DocumentsContract methods");

        // a folder inside the tree names its own document; the tree itself only the tree's
        const bool isDocument = address.contains ("/document/");
        LocalRef<jstring> folderId ((jstring) env->CallStaticObjectMethod (contract.get(), isDocument ? getDocId : getTreeId, uri.get()));
        if (failed (env) || folderId == nullptr) return fail ("not a folder from the folder picker");
        LocalRef<jobject> childrenUri (env->CallStaticObjectMethod (contract.get(), childrenOf, uri.get(), folderId.get()));
        if (failed (env) || childrenUri == nullptr) return fail ("the folder's contents could not be addressed");

        LocalRef<jclass> stringClass (env->FindClass ("java/lang/String"));
        LocalRef<jobjectArray> columns (env->NewObjectArray (4, stringClass.get(), nullptr));
        const char* names[] = { "document_id", "_display_name", "mime_type", "_size" };
        for (int i = 0; i < 4; ++i)
            env->SetObjectArrayElement (columns.get(), i, javaString (names[i]).get());
        if (failed (env)) return fail ("out of memory");

        LocalRef<jclass> resolverClass (env->GetObjectClass (resolver.get()));
        const auto query = env->GetMethodID (resolverClass.get(), "query",
                                             "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;");
        if (failed (env) || query == nullptr) return fail ("no ContentResolver.query");
        LocalRef<jobject> cursor (env->CallObjectMethod (resolver.get(), query, childrenUri.get(), columns.get(), nullptr, nullptr, nullptr));
        if (failed (env) || cursor == nullptr) return fail ("the provider would not list the folder");

        LocalRef<jclass> cursorClass (env->GetObjectClass (cursor.get()));
        const auto moveToNext = env->GetMethodID (cursorClass.get(), "moveToNext", "()Z");
        const auto getString = env->GetMethodID (cursorClass.get(), "getString", "(I)Ljava/lang/String;");
        const auto getLong = env->GetMethodID (cursorClass.get(), "getLong", "(I)J");
        const auto isNull = env->GetMethodID (cursorClass.get(), "isNull", "(I)Z");
        const auto close = env->GetMethodID (cursorClass.get(), "close", "()V");
        if (failed (env) || moveToNext == nullptr || getString == nullptr || getLong == nullptr || isNull == nullptr || close == nullptr)
            return fail ("no Cursor methods");

        auto text = [env] (jobject jstr) { return jstr != nullptr ? juceString ((jstring) jstr) : String(); };
        while (env->CallBooleanMethod (cursor.get(), moveToNext) && ! failed (env))
        {
            LocalRef<jstring> id ((jstring) env->CallObjectMethod (cursor.get(), getString, (jint) 0));
            LocalRef<jstring> name ((jstring) env->CallObjectMethod (cursor.get(), getString, (jint) 1));
            LocalRef<jstring> mime ((jstring) env->CallObjectMethod (cursor.get(), getString, (jint) 2));
            const bool noSize = env->CallBooleanMethod (cursor.get(), isNull, (jint) 3);
            const jlong size = noSize ? (jlong) -1 : env->CallLongMethod (cursor.get(), getLong, (jint) 3);
            if (failed (env) || id == nullptr)
                continue;
            LocalRef<jobject> childUri (env->CallStaticObjectMethod (contract.get(), documentOf, uri.get(), id.get()));
            if (failed (env) || childUri == nullptr)
                continue;
            LocalRef<jstring> childAddress ((jstring) env->CallObjectMethod (childUri.get(), uriToString));
            if (failed (env) || childAddress == nullptr)
                continue;
            ChildDocument c;
            c.url = URL (text (childAddress.get()));
            c.name = text (name.get());
            c.mime = text (mime.get());
            c.size = (int64) size;
            c.isFolder = c.mime == "vnd.android.document/directory";
            out.push_back (std::move (c));
        }
        env->CallVoidMethod (cursor.get(), close);
        failed (env);
        return out;
    }

    String replaceDocument (const URL& document, const void* data, size_t size)
    {
        if (document.isEmpty()) return "no document";
        if (data == nullptr || size == 0) return "nothing to write";

        size_t done = 0;
        const auto truncating = writeWithMode (document, [&] (void* dest, int max)
        {
            const auto n = std::min<size_t> ((size_t) max, size - done);
            std::memcpy (dest, static_cast<const char*> (data) + done, n);
            done += n;
            return (int) n;
        }, "wt");
        if (truncating.isEmpty())
            return {};

        // a provider that does not know "wt": JUCE's own way ("w")
        std::unique_ptr<OutputStream> out;
        if (auto doc = AndroidDocument::fromDocument (document); doc.hasValue())
            out = doc.createOutputStream();
        if (out == nullptr)
            return truncating;
        if (! out->write (data, size))
            return "writing failed";
        out->flush();
        return {};
    }

    String replaceDocument (const URL& document, const File& source)
    {
        if (document.isEmpty()) return "no document";
        FileInputStream in (source);
        if (in.failedToOpen()) return "the rendered file could not be read";

        const auto truncating = writeWithMode (document, [&in] (void* dest, int max) { return in.read (dest, max); }, "wt");
        if (truncating.isEmpty())
            return {};

        // a provider that does not know "wt": JUCE's own way ("w")
        in.setPosition (0);
        std::unique_ptr<OutputStream> out;
        if (auto doc = AndroidDocument::fromDocument (document); doc.hasValue())
            out = doc.createOutputStream();
        if (out == nullptr)
            return truncating;
        if (out->writeFromInputStream (in, -1) != source.getSize())
            return "writing failed";
        out->flush();
        return {};
    }
}
#endif
