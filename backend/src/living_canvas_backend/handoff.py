from typing import Dict
from urllib.parse import urlencode, urlparse


PLATFORMS: Dict[str, tuple] = {
    "eleme": ("https://h5.ele.me/search/", "keyword"),
}
ALLOWED_HOSTS = {"h5.ele.me"}


def build_platform_search_url(platform: str, search_query: str) -> str:
    if platform not in PLATFORMS:
        raise ValueError("unsupported_platform")
    base_url, query_key = PLATFORMS[platform]
    return base_url + "?" + urlencode({query_key: search_query})


def is_allowed_url(url: str) -> bool:
    parsed = urlparse(url)
    return parsed.scheme == "https" and parsed.hostname in ALLOWED_HOSTS
