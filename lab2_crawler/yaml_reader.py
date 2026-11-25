def parse_value(text):
    text = text.strip()
    if text == "":
        return ""
    if text.startswith('"') and text.endswith('"'):
        return text[1:-1]
    if text.startswith("'") and text.endswith("'"):
        return text[1:-1]
    if text == "true":
        return True
    if text == "false":
        return False
    try:
        return int(text)
    except ValueError:
        pass
    try:
        return float(text)
    except ValueError:
        pass
    return text


def read_yaml(path):
    f = open(path, encoding="utf-8")
    lines = f.readlines()
    f.close()
    return parse_yaml_lines(lines)


def parse_yaml_lines(lines):
    result = {}
    section = None
    last_key = None
    for raw_line in lines:
        line = raw_line.rstrip("\n")
        if "#" in line:
            line = line[: line.index("#")]
        if line.strip() == "":
            continue
        indent = len(line) - len(line.lstrip(" "))
        content = line.strip()
        if indent == 0:
            if content.endswith(":"):
                section = content[:-1]
                result[section] = {}
                last_key = None
            else:
                key, value = content.split(":", 1)
                result[key.strip()] = parse_value(value)
                section = None
            continue
        if section is None:
            continue
        if content.startswith("- "):
            if last_key is None:
                continue
            if not isinstance(result[section][last_key], list):
                result[section][last_key] = []
            result[section][last_key].append(parse_value(content[2:]))
            continue
        key, value = content.split(":", 1)
        last_key = key.strip()
        result[section][last_key] = parse_value(value)
    return result
