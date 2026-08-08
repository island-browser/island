"""Read-only official dependency update checks."""

from __future__ import annotations

from dataclasses import dataclass
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from deps.model import Artifact, DependencyError


@dataclass(frozen=True, slots=True)
class UpdateError(DependencyError):
    detail: str

    def __str__(self) -> str:
        return f"Dependency update check failed: {self.detail}"


def _allowlisted(url: str, hosts: frozenset[str]) -> bool:
    parsed = urlparse(url)
    return parsed.scheme == "https" and parsed.hostname in hosts


def parse_sha256_sidecar(content: bytes) -> str:
    """Parse the first digest field in an official SHA-256 sidecar."""
    token = content.decode("ascii", "strict").strip().split(maxsplit=1)[0]
    if len(token) != 64 or any(character not in "0123456789abcdef" for character in token):
        raise UpdateError("official sidecar is not a lowercase SHA-256 digest")
    return token


def compare_digest(expected: str, candidate: str) -> str:
    """Classify an official candidate without changing the lock."""
    return "current" if expected == candidate else "changed"


def check_artifact(artifact: Artifact, hosts: frozenset[str]) -> str:
    """Fetch a pinned CEF sidecar or confirm an immutable source is reachable."""
    url = f"{artifact.url}.sha256" if artifact.name == "cef" else artifact.url
    if not _allowlisted(url, hosts):
        raise UpdateError("configured URL is not allowlisted")
    try:
        with urlopen(Request(url, method="GET"), timeout=60) as response:
            if not _allowlisted(response.geturl(), hosts):
                raise UpdateError("official response redirected outside the allowlist")
            content = response.read(512)
    except (HTTPError, URLError, OSError) as error:
        raise UpdateError(str(error)) from error
    if artifact.name == "cef":
        return compare_digest(artifact.sha256, parse_sha256_sidecar(content))
    return "current"
