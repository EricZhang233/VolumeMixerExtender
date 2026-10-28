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
            L"about.vbcable", L"about.eartrumpet", L"about.microsoft" })
            result += L"\n\n" + text.Resolve(key);
        log::Logger::Instance().WriteKey(log::Level::Info, L"about", L"log.about.opened");
        return result;
    }
}
