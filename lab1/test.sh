#!/bin/sh
#
# Compares the output of the four utilities against the system ones.
# Run it after "make", or simply as "make test".

set -u

LC_ALL=C
export LC_ALL

fail=0
work=$(mktemp -d)
here=$(pwd)
trap 'rm -rf "$work"' EXIT

report() # report NAME EXPECTED_FILE ACTUAL_FILE
{
	if cmp -s "$2" "$3"; then
		printf '  ok    %s\n' "$1"
	else
		printf '  FAIL  %s\n' "$1"
		diff -u "$2" "$3" | sed -n '1,20p' | sed 's/^/        /'
		fail=1
	fi
}

# ---------------------------------------------------------------- fixtures
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
report 'cat -n on a file without a trailing newline' "$work/want" "$work/got"

cat "$work/text.txt" "$work/text.txt"           > "$work/want"
"$here/mycat" "$work/text.txt" "$work/text.txt" > "$work/got"
report 'cat over two files (continuous numbering)' "$work/want" "$work/got"

cat "$work/text.txt" | cat -n           > "$work/want"
cat "$work/text.txt" | "$here/mycat" -n > "$work/got"
report 'cat reading standard input' "$work/want" "$work/got"

# ---------------------------------------------------------------- mygrep
echo 'mygrep:'
for args in 'alpha' '-i ALPHA' '-v alpha' '-n alpha' 'a.*a' '^beta'; do
	# shellcheck disable=SC2086
	grep $args "$work/text.txt"           > "$work/want" 2>&1
	# shellcheck disable=SC2086
	"$here/mygrep" $args "$work/text.txt" > "$work/got"  2>&1
	report "grep $args text.txt" "$work/want" "$work/got"
done

# the pipeline requirement of the first practical work
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
	printf '  ok    exit status when nothing matches (%s)\n' "$got_rc"
else
	printf '  FAIL  exit status when nothing matches: want %s, got %s\n' "$want_rc" "$got_rc"
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
report 'ls -la over two directories' "$work/want" "$work/got"

ls -l "$work/dir/plain.txt"           > "$work/want" 2>&1
"$here/myls" -l "$work/dir/plain.txt" > "$work/got"  2>&1
report 'ls -l on a single file' "$work/want" "$work/got"

# ---------------------------------------------------------------- mychmod
echo 'mychmod:'
for spec in '644' '766' '0700' '+x' 'u-r' 'g+rw' 'ug+rw' 'uga+rwx' 'a=r' 'o-rwx' 'u+rw,go-w'; do
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

# the same, but starting from a mode that already has the execute bits set
for spec in '+x' 'a=rx' 'u=rwx,go=' ; do
	printf 'x\n' > "$work/a.txt"
	printf 'x\n' > "$work/b.txt"
	chmod 755 "$work/a.txt"
	chmod 755 "$work/b.txt"

	chmod "$spec" "$work/a.txt"           2>/dev/null
	"$here/mychmod" "$spec" "$work/b.txt" 2>/dev/null

	stat -c '%a' "$work/a.txt" > "$work/want"
	stat -c '%a' "$work/b.txt" > "$work/got"
	report "chmod $spec (starting from 755)" "$work/want" "$work/got"
done

# a mode that does not parse must be refused, and must not touch the file
"$here/mychmod" 'q+z' "$work/b.txt" > /dev/null 2>&1
if [ $? -eq 2 ]; then
	printf '  ok    invalid mode is rejected\n'
else
	printf '  FAIL  invalid mode was not rejected\n'
	fail=1
fi

# ----------------------------------------------------------------
echo
if [ "$fail" -eq 0 ]; then
	echo 'all checks passed'
else
	echo 'there are failures'
fi
exit "$fail"
