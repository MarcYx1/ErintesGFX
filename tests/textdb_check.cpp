// Self-check for TextDb raw texts.xml patching. Run: build/textdb_check
#include "../src/project.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>

static TextDb make(const QString& nl)
{
    QString x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<TextDatabase>\n  <Languages>\n    <Language Id=\"GB\" />\n"
                "  </Languages>\n  <Texts>\n    <TextGroup Id=\"Unsorted\">\n"
                "      <Text Id=\"__SingleUse_AB\" Alignment=\"Left\" TypographyId=\"Default\">\n"
                "        <Translation Language=\"GB\">x</Translation>\n      </Text>\n"
                "      <Text Id=\"__SingleUse_ABC\" Alignment=\"Center\" TypographyId=\"Large\">\n"
                "        <Translation Language=\"GB\" Alignment=\"Center\">y</Translation>\n      </Text>\n"
                "    </TextGroup>\n  </Texts>\n  <Typographies>\n"
                "    <Typography Id=\"Default\" Font=\"verdana.ttf\" Size=\"20\" Bpp=\"4\" />\n  </Typographies>\n</TextDatabase>\n";
    x.replace("\n", nl);
    TextDb t;
    t.setRaw(x);
    return t;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    for (const QString nl : {QString("\n"), QString("\r\n")}) {
        TextDb t = make(nl);
        assert(t.language == "GB" && t.get("__SingleUse_ABC").text == "y");

        // backslashes and \1 stay literal, attribute-like text is not rewritten
        t.set("__SingleUse_AB", {"Right", "Default", "C:\\dir \\1 Alignment=\"Left\"\nline2"});
        assert(t.get("__SingleUse_AB").text == "C:\\dir \\1 Alignment=\"Left\"\nline2");
        assert(t.get("__SingleUse_AB").alignment == "Right");
        assert(t.get("__SingleUse_ABC").text == "y"); // prefix id untouched

        // per-language Alignment follows the text's alignment
        t.set("__SingleUse_ABC", {"Left", "Large", "z"});
        assert(t.raw.contains("<Translation Language=\"GB\" Alignment=\"Left\">z</Translation>"));

        const QString id = t.add({"Left", "Default", "new <> & text"});
        assert(t.get(id).text == "new <> & text");
        assert(nl == "\n" ? !t.raw.contains("\r") : t.raw.count("\r\n") == t.raw.count("\n")); // one line ending style

        const QString before = make(nl).raw;
        t.remove(id);
        t.remove("__SingleUse_ABC");
        assert(!t.has(id) && !t.has("__SingleUse_ABC") && t.has("__SingleUse_AB"));
        assert(!t.raw.contains(nl + nl)); // no blank lines left behind
        assert(t.raw.size() < before.size());
    }
    puts("textdb_check OK");
    return 0;
}
