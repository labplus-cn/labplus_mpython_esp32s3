"""Small helpers for messages selected by the firmware build language."""

try:
    from firmware_language import IS_ENGLISH
except ImportError:
    IS_ENGLISH = False


def localized_text(chinese, english):
    """Return the message selected by the firmware language."""
    return english if IS_ENGLISH else chinese


def print_i18n(chinese, english, *args, **kwargs):
    """Print one localized message, defaulting to Chinese without config."""
    printer = kwargs.pop("printer", None) or print
    printer(localized_text(chinese, english), *args, **kwargs)
