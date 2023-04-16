def pascal_generator(s: str) -> str:
    """Turns a snake_case str into a pascalCase str"""
    split_s = s.split("_")
    return split_s[0] + "".join(word.capitalize() for word in split_s[1:])
