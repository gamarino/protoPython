def gettext(message):
    return message

def ngettext(singular, plural, n):
    if n == 1:
        return singular
    return plural

def dgettext(domain, message):
    return message

def dngettext(domain, singular, plural, n):
    if n == 1:
        return singular
    return plural
