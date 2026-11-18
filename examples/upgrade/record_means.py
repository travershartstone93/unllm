import math
import sys

def checked_number(value):
    if not math.isfinite(value) or abs(value) > 1000000000000000.0:
        raise ValueError('number outside the finite +/-1e15 domain')
    return value

def parse_number(text):
    text = text.strip()
    if not text or '_' in text or any((c in text for c in 'xXpP')):
        raise ValueError('expected a decimal number')
    return checked_number(float(text))

def checked_line(raw, index):
    if index > 100000:
        raise ValueError('input exceeds 100000 rows')
    line = raw.removesuffix('\n').removesuffix('\r')
    if len(line) > 16384 or any((ord(c) < 32 and c != '\t' or ord(c) >= 127 for c in line)):
        raise ValueError('expected printable ASCII and tabs, at most 16384 characters')
    return line

def format_number(value):
    return format(checked_number(value), '.17g')

def read_input(stream):
    data = {}
    for index, raw in enumerate(stream, 1):
        if index > 100000:
            raise ValueError('input exceeds 100000 rows')
        try:
            line = checked_line(raw, index)
            if not line.strip():
                continue
            group, separator, value = line.partition('\t')
            group = group.strip()
            if not separator or not group or '\t' in value:
                raise ValueError('expected key TAB number')
            number = parse_number(value)
            data.setdefault(group, []).append(number)
        except ValueError as error:
            raise ValueError('line ' + str(index) + ': ' + str(error)) from error
    return data

def transform(values):
    values = [x for x in values if 10.0 > x]
    values = list(reversed(values))
    return values

def render_values(values):
    if not values:
        raise ValueError('no numeric values remain')
    total = 0.0
    for value in values:
        total += value
    average = checked_number(total / len(values))
    return [format_number(average)]

def process_data(stream):
    data = read_input(stream)
    output = []
    for group in sorted(data):
        values = transform(data[group])
        if not values:
            continue
        for row in render_values(values):
            output.append(group + '\t' + row)
    return output

def main():
    try:
        arguments = sys.argv[1:]
        if len(arguments) > 1:
            raise ValueError('usage: program [input-file]')
        if arguments:
            with open(arguments[0], 'r', encoding='ascii') as stream:
                output = process_data(stream)
        else:
            output = process_data(sys.stdin)
        for line in output:
            print(line)
        return 0
    except (OSError, ValueError, OverflowError) as error:
        print('error: ' + str(error), file=sys.stderr)
        return 2
if __name__ == '__main__':
    raise SystemExit(main())
