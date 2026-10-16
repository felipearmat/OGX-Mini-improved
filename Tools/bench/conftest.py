"""Bench tests against a real adapter on USB, in D-Input/Debug mode (Tools/bench/README.md)."""
import json
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from adapter import Adapter, CONTRACT  # noqa: E402

CAPS = CONTRACT["capabilities"]


@pytest.fixture
def adapter():
    a = Adapter.open()
    yield a
    a.close()


@pytest.fixture
def caps(adapter):
    mask = adapter.capabilities()
    assert mask is not None, "the adapter does not answer GET_CAPABILITIES (firmware too old?)"
    return mask


@pytest.fixture
def hybrid(caps):
    return bool(caps & CAPS["HYBRID"])


def pytest_collection_modifyitems(config, items):
    for item in items:
        if "hybrid_only" in item.keywords:
            item.add_marker(pytest.mark.hybrid_only)


def pytest_configure(config):
    config.addinivalue_line("markers", "hybrid_only: needs the RP2040 + ESP32 hybrid")
    config.addinivalue_line("markers", "slow: takes a minute or more (flashing, restarts)")
