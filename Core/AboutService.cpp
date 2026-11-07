#include "AboutService.h"

#include "TextService.h"
#include "Strings.h"
#include "Logger.h"
#include "Version.h"

namespace vmex::about
{
    std::wstring Information()
    {
        const auto& text = text::Embedded();
        std::wstring result = strings::ToWide(VMEX_PRODUCT_NAME) + L"\n" +
            text.ResolveFormat(L"about.version", { strings::ToWide(VMEX_VERSION_STRING) });
        for (const auto key : { L"about.developer", L"about.project", L"about.license", L"about.thirdParty",
            L"about.microsoft.title", L"about.microsoft", L"about.link.microsoft.url",
            L"about.vbcable.title", L"about.vbcable", L"about.link.vbcable.url", L"about.link.vbcable.license.url" })
            result += L"\n\n" + text.Resolve(key);
        log::Logger::Instance().Write(log::Level::Info, L"about", L"About information requested");
        return result;
    }
}
