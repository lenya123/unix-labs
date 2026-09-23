#!/bin/sh
#
# Сравнивает вывод моих утилит с системными на одних и тех же данных.
# Запуск: make test

set -u

LC_ALL=C
export LC_ALL

fail=0
work=$(mktemp -d)
here=$(pwd)
trap 'rm -rf "$work"' EXIT

report() # report ИМЯ ФАЙЛ_ОЖИДАЕМОГО ФАЙЛ_ПОЛУЧЕННОГО
{
	if cmp -s "$2" "$3"; then
		printf '  ok    %s\n' "$1"
	else
		printf '  FAIL  %s\n' "$1"
		diff -u "$2" "$3" | sed -n '1,20p' | sed 's/^/        /'
		fail=1
	fi
}

# -------------------------------------------------------- подопытные файлы
printf 'alpha\n\nbeta gamma\n\n\ndelta alpha\n' > "$work/text.txt"
printf 'no newline at the end' > "$work/raw.txt"
mkdir -p "$work/dir/sub"
printf 'x\n' > "$work/dir/plain.txt"
printf '#!/bin/sh\necho hi\n' > "$work/dir/script.sh"
chmod 755 "$work/dir/script.sh"
ln -s plain.txt "$work/dir/link.txt"
printf 'hidden\n' > "$work/dir/.dotfile"

# ---------------------------------------------------------------- mycat
echo 'mycat:'
for flags in '' '-n' '-b' '-E' '-nE' '-b -E'; do
	# shellcheck disable=SC2086
	cat $flags "$work/text.txt"           > "$work/want" 2>&1
	# shellcheck disable=SC2086
	"$here/mycat" $flags "$work/text.txt" > "$work/got"  2>&1
	report "cat $flags text.txt" "$work/want" "$work/got"
done

cat -n "$work/raw.txt"           > "$work/want"
"$here/mycat" -n "$work/raw.txt" > "$work/got"
report 'cat -n на файле без перевода строки в конце' "$work/want" "$work/got"

cat "$work/text.txt" "$work/text.txt"           > "$work/want"
"$here/mycat" "$work/text.txt" "$work/text.txt" > "$work/got"
report 'cat по двум файлам (сквозная нумерация)' "$work/want" "$work/got"

cat "$work/text.txt" | cat -n           > "$work/want"
cat "$work/text.txt" | "$here/mycat" -n > "$work/got"
report 'cat читает стандартный ввод' "$work/want" "$work/got"

# ---------------------------------------------------------------- mygrep
echo 'mygrep:'
for args in 'alpha' '-i ALPHA' '-v alpha' '-n alpha' 'a.*a' '^beta'; do
	# shellcheck disable=SC2086
	grep $args "$work/text.txt"           > "$work/want" 2>&1
	# shellcheck disable=SC2086
	"$here/mygrep" $args "$work/text.txt" > "$work/got"  2>&1
	report "grep $args text.txt" "$work/want" "$work/got"
done

# конвейер - требование первой практической работы
"$here/mycat" "$work/text.txt" | "$here/mygrep" alpha > "$work/got"
cat "$work/text.txt" | grep alpha                     > "$work/want"
report 'mycat file | mygrep pattern' "$work/want" "$work/got"

ls -l "$work/dir" | "$here/mygrep" script > "$work/got"
ls -l "$work/dir" | grep script           > "$work/want"
report 'ls -l | mygrep pattern' "$work/want" "$work/got"

grep nothing-matches-this "$work/text.txt" > /dev/null 2>&1
want_rc=$?
"$here/mygrep" nothing-matches-this "$work/text.txt" > /dev/null 2>&1
got_rc=$?
if [ "$want_rc" = "$got_rc" ]; then
	printf '  ok    код возврата, когда ничего не нашлось (%s)\n' "$got_rc"
else
	printf '  FAIL  код возврата, когда ничего не нашлось: ждали %s, получили %s\n' "$want_rc" "$got_rc"
	fail=1
fi

# ---------------------------------------------------------------- myls
echo 'myls:'
for flags in '' '-a' '-l' '-la' '-l -a'; do
	# shellcheck disable=SC2086
	ls $flags "$work/dir"           > "$work/want" 2>&1
	# shellcheck disable=SC2086
	"$here/myls" $flags "$work/dir" > "$work/got"  2>&1
	report "ls $flags dir" "$work/want" "$work/got"
done

ls -la "$work/dir" "$work/dir/sub"           > "$work/want" 2>&1
"$here/myls" -la "$work/dir" "$work/dir/sub" > "$work/got"  2>&1
report 'ls -la по двум каталогам' "$work/want" "$work/got"

ls -l "$work/dir/plain.txt"           > "$work/want" 2>&1
"$here/myls" -l "$work/dir/plain.txt" > "$work/got"  2>&1
report 'ls -l на одном файле' "$work/want" "$work/got"

# в задании флаги пишут и после имени каталога - getopt их переставляет
ls -l "$work/dir" -a           > "$work/want" 2>&1
"$here/myls" -l "$work/dir" -a > "$work/got"  2>&1
report 'ls -l dir -a (флаги после имени каталога)' "$work/want" "$work/got"

ls "$work/dir" -la           > "$work/want" 2>&1
"$here/myls" "$work/dir" -la > "$work/got"  2>&1
report 'ls dir -la (флаги после имени каталога)' "$work/want" "$work/got"

# ------------------------------------------------------- myls, теперь в терминале
# Всё выше пишет вывод в файл, а туда ls печатает по одному имени в строку и без
# цветов. Колонки и цвета видны только в терминале, поэтому нужен псевдотерминал.
echo 'myls в терминале:'
if command -v script > /dev/null 2>&1; then
	cdir="$work/cols"
	mkdir -p "$cdir/somedir" "$cdir/another-directory"
	# имена нарочно разной длины - именно на них колонки и разъезжаются
	for n in a bb ccc README.md notes.txt a-very-long-file-name-here.txt \
	         x yy zzz Makefile src lib docs build.sh tmp q; do
		: > "$cdir/$n"
	done
	chmod +x "$cdir/build.sh"
	ln -s notes.txt "$cdir/shortcut"

	# каталог, открытый на запись всем, ls красит иначе, чем обычный
	mkdir -p "$cdir/open-to-all" "$cdir/sticky-open" "$cdir/sticky-only"
	chmod 777 "$cdir/open-to-all"
	chmod 1777 "$cdir/sticky-open"
	chmod 1755 "$cdir/sticky-only"

	for flags in '' '-a'; do
		script -qec "ls --color=auto $flags '$cdir'" /dev/null > "$work/want"
		script -qec "'$here/myls' $flags '$cdir'" /dev/null > "$work/got"
		report "ls --color=auto $flags dir (колонки и цвета)" "$work/want" "$work/got"
	done

	# тот же список в узком терминале должен перестроиться так же
	COLUMNS=40 script -qec "ls --color=auto '$cdir'" /dev/null > "$work/want"
	COLUMNS=40 script -qec "'$here/myls' '$cdir'" /dev/null > "$work/got"
	report 'ls в терминале шириной 40' "$work/want" "$work/got"

	# на нескольких каталогах сброс цвета всё равно должен быть один
	script -qec "ls --color=auto '$cdir' '$cdir/somedir'" /dev/null > "$work/want"
	script -qec "'$here/myls' '$cdir' '$cdir/somedir'" /dev/null > "$work/got"
	report 'ls по двум каталогам в терминале' "$work/want" "$work/got"
else
	echo '  мимо  нет команды script, псевдотерминал не открыть'
fi

# ---------------------------------------------------------------- mychmod
echo 'mychmod:'
for spec in '644' '766' '0700' '+x' 'u-r' 'g+rw' 'ug+rw' 'uga+rwx' 'a=r' 'o-rwx' 'u+rw,go-w'; do
	# прошлый круг мог оставить файлы только для чтения - начинаем с нуля
	rm -f "$work/a.txt" "$work/b.txt"
	printf 'x\n' > "$work/a.txt"
	printf 'x\n' > "$work/b.txt"
	chmod 644 "$work/a.txt"
	chmod 644 "$work/b.txt"

	chmod "$spec" "$work/a.txt"           2>/dev/null
	"$here/mychmod" "$spec" "$work/b.txt" 2>/dev/null

	stat -c '%a' "$work/a.txt" > "$work/want"
	stat -c '%a' "$work/b.txt" > "$work/got"
	report "chmod $spec" "$work/want" "$work/got"
done

# то же самое, но начиная с прав, где биты выполнения уже стоят
for spec in '+x' 'a=rx' 'u=rwx,go=' ; do
	# прошлый круг мог оставить файлы только для чтения - начинаем с нуля
	rm -f "$work/a.txt" "$work/b.txt"
	printf 'x\n' > "$work/a.txt"
	printf 'x\n' > "$work/b.txt"
	chmod 755 "$work/a.txt"
	chmod 755 "$work/b.txt"

	chmod "$spec" "$work/a.txt"           2>/dev/null
	"$here/mychmod" "$spec" "$work/b.txt" 2>/dev/null

	stat -c '%a' "$work/a.txt" > "$work/want"
	stat -c '%a' "$work/b.txt" > "$work/got"
	report "chmod $spec (начиная с 755)" "$work/want" "$work/got"
done

# неразбираемый режим должен быть отвергнут, а файл - не тронут
"$here/mychmod" 'q+z' "$work/b.txt" > /dev/null 2>&1
if [ $? -eq 2 ]; then
	printf '  ok    неверный режим отвергнут\n'
else
	printf '  FAIL  неверный режим не отвергнут\n'
	fail=1
fi

# ----------------------------------------------------------------
echo
if [ "$fail" -eq 0 ]; then
	echo 'все проверки прошли'
else
	echo 'есть расхождения'
fi
exit "$fail"
