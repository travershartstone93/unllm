#!/bin/bash
# The discoverer must follow installed API metadata, including ownership and an exact MIME name.
set -euo pipefail
emitter=$(realpath -e "$1")
work=$(mktemp -d); trap 'rm -rf -- "$work"' EXIT; cd "$work"
echo '{"gap":{"from":"in:text/example","to":"strings"}}' > gaps
cat > function.xml <<'XML'
<function c:identifier="g_fixture_reader"><doc>Decodes text/example into records</doc>
<parameters><parameter><type name="utf8" c:type="const gchar*"/></parameter></parameters>
<return-value transfer-ownership="full"><array c:type="gchar**"><type name="utf8"/></array></return-value></function>
XML
gir() {
    { echo '<repository xmlns:c="http://www.gtk.org/introspection/c/1.0"><package name="glib-2.0"/><namespace name="GLib">'
      cat "$@"; echo '</namespace></repository>'; } > fixture.gir
}
reject() { if "$emitter" gaps fixture.gir whole-file >/dev/null 2>&1; then echo 'Bad metadata accepted' >&2; exit 1; fi; }
gir function.xml function.xml
"$emitter" gaps fixture.gir whole-file | jq -e '.symbol=="g_fixture_reader" and (.source|contains("g_fixture_reader(text)"))' >/dev/null
for change in 's/text\/example/text\/example-list/g' 's/ownership="full"/ownership="none"/' 's/<array /<array zero-terminated="0" /' 's/<function /<function throws="1" /'; do
    sed "$change" function.xml > altered; gir altered; reject
done
sed 's/g_fixture_reader/g_another_reader/' function.xml > another
gir function.xml another; reject
