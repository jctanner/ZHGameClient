from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_INSTALL_ROOT = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour"
)


def _normalize_member_path(value: str) -> str:
    return value.replace("/", "\\").strip().lower()


def _clean_ini_value(value: str) -> str:
    raw = value.strip()
    if ";" in raw:
        raw = raw.split(";", 1)[0].rstrip()
    return raw


@dataclass(frozen=True)
class BigMember:
    archive_path: Path
    member_path: str
    data_offset: int
    size: int


class BigArchive:
    def __init__(self, archive_path: Path) -> None:
        self.archive_path = archive_path
        self.members: list[BigMember] = self._read_index()

    def _read_index(self) -> list[BigMember]:
        members: list[BigMember] = []
        with self.archive_path.open("rb") as handle:
            magic = handle.read(4)
            if magic != b"BIGF":
                raise ValueError(f"{self.archive_path} is not a BIGF archive")
            _archive_size = struct.unpack("<I", handle.read(4))[0]
            count = struct.unpack(">I", handle.read(4))[0]
            _first_offset = struct.unpack(">I", handle.read(4))[0]
            for _ in range(count):
                data_offset, size = struct.unpack(">II", handle.read(8))
                name_bytes = bytearray()
                while True:
                    chunk = handle.read(1)
                    if chunk in (b"", b"\x00"):
                        break
                    name_bytes.extend(chunk)
                member_path = name_bytes.decode("latin-1")
                members.append(BigMember(self.archive_path, member_path, data_offset, size))
        return members

    def read_member(self, member: BigMember) -> bytes:
        with self.archive_path.open("rb") as handle:
            handle.seek(member.data_offset)
            data = handle.read(member.size)
        if len(data) != member.size:
            raise ValueError(f"Short read for {member.member_path} from {self.archive_path}")
        return data


class IniDataRepository:
    def __init__(self, install_root: Path) -> None:
        self.install_root = install_root
        self.archives = self._load_archives()
        self.member_map = self._build_member_map()

    def _load_archives(self) -> list[BigArchive]:
        archive_names = ["INIZH.big", "PatchINI.big"]
        loaded: list[BigArchive] = []
        for name in archive_names:
            path = self.install_root / name
            if path.exists():
                loaded.append(BigArchive(path))
        if not loaded:
            raise FileNotFoundError(f"No INI BIG archives found under {self.install_root}")
        return loaded

    def _build_member_map(self) -> dict[str, BigMember]:
        member_map: dict[str, BigMember] = {}
        for archive in self.archives:
            for member in archive.members:
                member_map[_normalize_member_path(member.member_path)] = member
        return member_map

    def list_members(self, pattern: str | None = None) -> list[BigMember]:
        regex = re.compile(pattern, re.IGNORECASE) if pattern else None
        members = sorted(self.member_map.values(), key=lambda item: _normalize_member_path(item.member_path))
        if regex is None:
            return members
        return [member for member in members if regex.search(member.member_path)]

    def get_member(self, member_path: str) -> BigMember:
        key = _normalize_member_path(member_path)
        member = self.member_map.get(key)
        if member is None:
            raise KeyError(f"Archive member not found: {member_path}")
        return member

    def read_member_bytes(self, member_path: str) -> bytes:
        member = self.get_member(member_path)
        for archive in self.archives:
            if archive.archive_path == member.archive_path:
                return archive.read_member(member)
        raise KeyError(f"Archive handle missing for {member_path}")

    def read_member_text(self, member_path: str) -> str:
        return self.read_member_bytes(member_path).decode("latin-1")


def _extract_blocks(text: str, block_keyword: str) -> dict[str, str]:
    pattern = re.compile(rf"(?mi)^[ \t]*{re.escape(block_keyword)}[ \t]+([^=\r\n;]+?)\s*$")
    out: dict[str, str] = {}
    matches = list(pattern.finditer(text))
    for index, match in enumerate(matches):
        name = match.group(1).strip()
        body_start = match.end()
        body_end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[body_start:body_end]
        out[name] = body
    return out


def _extract_property_values(block_text: str, key: str) -> list[str]:
    pattern = re.compile(rf"(?mi)^[ \t]*{re.escape(key)}[ \t]*=[ \t]*(.+?)\s*$")
    return [_clean_ini_value(match.group(1)) for match in pattern.finditer(block_text)]


def _first_property(block_text: str, key: str) -> str | None:
    values = _extract_property_values(block_text, key)
    return values[0] if values else None


def _commandset_buttons(block_text: str) -> list[str]:
    buttons: list[str] = []
    pattern = re.compile(r"(?mi)^[ \t]*\d+[ \t]*=[ \t]*(.+?)\s*$")
    for match in pattern.finditer(block_text):
        buttons.append(match.group(1).strip())
    return buttons


def _collect_matching_blocks(blocks: dict[str, str], names: Iterable[str]) -> dict[str, str]:
    wanted = {name.lower() for name in names}
    return {name: body for name, body in blocks.items() if name.lower() in wanted}


def build_gla_tech_report(repo: IniDataRepository) -> dict[str, object]:
    command_button_text = repo.read_member_text(r"Data\INI\CommandButton.ini")
    command_set_text = repo.read_member_text(r"Data\INI\CommandSet.ini")
    player_template_text = repo.read_member_text(r"Data\INI\PlayerTemplate.ini")
    rank_text = repo.read_member_text(r"Data\INI\Rank.ini")
    science_text = repo.read_member_text(r"Data\INI\Science.ini")
    upgrade_text = repo.read_member_text(r"Data\INI\Upgrade.ini")

    command_buttons = _extract_blocks(command_button_text, "CommandButton")
    command_sets = _extract_blocks(command_set_text, "CommandSet")
    player_templates = _extract_blocks(player_template_text, "PlayerTemplate")
    rank_blocks = _extract_blocks(rank_text, "Rank")
    sciences = _extract_blocks(science_text, "Science")
    upgrades = _extract_blocks(upgrade_text, "Upgrade")

    object_members = repo.list_members(r"^Data\\INI\\Object\\.*GLA.*\.ini$")
    object_command_sets: dict[str, list[str]] = {}
    palace_names: list[str] = []
    black_market_names: list[str] = []
    for member in object_members:
        text = repo.read_member_text(member.member_path)
        objects = _extract_blocks(text, "Object")
        for object_name, body in objects.items():
            lower_name = object_name.lower()
            if "palace" not in lower_name and "blackmarket" not in lower_name:
                continue
            values = _extract_property_values(body, "CommandSet")
            values.extend(_extract_property_values(body, "CommandSetUpgrade"))
            if values:
                object_command_sets[object_name] = values
            if "palace" in lower_name:
                palace_names.append(object_name)
            if "blackmarket" in lower_name:
                black_market_names.append(object_name)

    def summarize_buttons(button_names: Iterable[str]) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for button_name in button_names:
            block = command_buttons.get(button_name)
            if block is None:
                rows.append({"command_button": button_name, "missing": True})
                continue
            rows.append(
                {
                    "command_button": button_name,
                    "command": _first_property(block, "Command"),
                    "upgrade": _first_property(block, "Upgrade"),
                    "science": _first_property(block, "Science"),
                    "special_power": _first_property(block, "SpecialPower"),
                    "object": _first_property(block, "Object"),
                    "text_label": _first_property(block, "TextLabel"),
                    "button_image": _first_property(block, "ButtonImage"),
                }
            )
        return rows

    def summarize_command_sets(set_names: Iterable[str]) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for set_name in sorted(set(set_names)):
            block = command_sets.get(set_name)
            if block is None:
                rows.append({"command_set": set_name, "missing": True})
                continue
            buttons = _commandset_buttons(block)
            rows.append(
                {
                    "command_set": set_name,
                    "buttons": summarize_buttons(buttons),
                }
            )
        return rows

    gla_template_names = [
        name
        for name in player_templates
        if "gla" in name.lower() or "slth" in name.lower() or "chem" in name.lower() or "demo" in name.lower()
    ]
    gla_templates: dict[str, dict[str, object]] = {}
    science_command_sets: set[str] = set()
    for name in sorted(gla_template_names):
        body = player_templates[name]
        rank1 = _first_property(body, "PurchaseScienceCommandSetRank1")
        rank3 = _first_property(body, "PurchaseScienceCommandSetRank3")
        rank8 = _first_property(body, "PurchaseScienceCommandSetRank8")
        for value in (rank1, rank3, rank8):
            if value:
                science_command_sets.add(value)
        gla_templates[name] = {
            "rank1_command_set": rank1,
            "rank3_command_set": rank3,
            "rank8_command_set": rank8,
        }

    science_buttons: set[str] = set()
    for set_name in science_command_sets:
        block = command_sets.get(set_name)
        if block is None:
            continue
        for button_name in _commandset_buttons(block):
            science_buttons.add(button_name)

    science_names: set[str] = set()
    for button_name in science_buttons:
        block = command_buttons.get(button_name)
        if block is None:
            continue
        science_name = _first_property(block, "Science")
        if science_name:
            science_names.add(science_name)

    rank_rows: list[dict[str, object]] = []
    for name, body in sorted(rank_blocks.items()):
        science_values = _extract_property_values(body, "SciencesGranted")
        if not science_values and _first_property(body, "SciencePurchasePointsGranted") is None:
            continue
        rank_rows.append(
            {
                "rank": name,
                "science_purchase_points_granted": _first_property(body, "SciencePurchasePointsGranted"),
                "sciences_granted": science_values,
            }
        )

    def summarize_named_entries(names: Iterable[str], source_blocks: dict[str, str], kind: str) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for name in sorted(set(names)):
            block = source_blocks.get(name)
            if block is None:
                rows.append({"name": name, "missing": True})
                continue
            rows.append(
                {
                    "name": name,
                    "display_name": _first_property(block, "DisplayName"),
                    "button_image": _first_property(block, "ButtonImage") if kind == "upgrade" else _first_property(block, "PurchaseButtonImage"),
                    "cost": _first_property(block, "BuildCost") if kind == "upgrade" else _first_property(block, "SciencePurchasePointCost"),
                    "prerequisite_science": _extract_property_values(block, "PrerequisiteSciences"),
                }
            )
        return rows

    palace_command_sets = [value for key, values in object_command_sets.items() if "palace" in key.lower() for value in values]
    black_market_command_sets = [value for key, values in object_command_sets.items() if "blackmarket" in key.lower() for value in values]

    palace_upgrade_names: set[str] = set()
    for row in summarize_command_sets(palace_command_sets):
        for button in row.get("buttons", []):
            upgrade_name = button.get("upgrade")
            if isinstance(upgrade_name, str) and upgrade_name:
                palace_upgrade_names.add(upgrade_name)

    black_market_upgrade_names: set[str] = set()
    for row in summarize_command_sets(black_market_command_sets):
        for button in row.get("buttons", []):
            upgrade_name = button.get("upgrade")
            if isinstance(upgrade_name, str) and upgrade_name:
                black_market_upgrade_names.add(upgrade_name)

    return {
        "install_root": str(repo.install_root),
        "archives": [str(archive.archive_path) for archive in repo.archives],
        "gla_player_templates": gla_templates,
        "gla_science_command_sets": summarize_command_sets(science_command_sets),
        "gla_sciences": summarize_named_entries(science_names, sciences, "science"),
        "rank_progression": rank_rows,
        "palace_objects": sorted(set(palace_names)),
        "palace_command_sets": summarize_command_sets(palace_command_sets),
        "palace_upgrades": summarize_named_entries(palace_upgrade_names, upgrades, "upgrade"),
        "black_market_objects": sorted(set(black_market_names)),
        "black_market_command_sets": summarize_command_sets(black_market_command_sets),
        "black_market_upgrades": summarize_named_entries(black_market_upgrade_names, upgrades, "upgrade"),
    }


def inspect_named_block(repo: IniDataRepository, file_path: str, block_keyword: str, block_name: str) -> dict[str, object]:
    text = repo.read_member_text(file_path)
    blocks = _extract_blocks(text, block_keyword)
    block = blocks.get(block_name)
    if block is None:
        raise KeyError(f"{block_keyword} {block_name} not found in {file_path}")
    lines = [line.rstrip() for line in block.splitlines()]
    return {
        "file": file_path,
        "block_keyword": block_keyword,
        "block_name": block_name,
        "properties": {
            line.split("=", 1)[0].strip(): line.split("=", 1)[1].strip()
            for line in lines
            if "=" in line and not line.lstrip().startswith(";")
        },
        "raw_block": block.strip(),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Inspect Zero Hour INI data from BIG archives.")
    parser.add_argument(
        "--install-root",
        type=Path,
        default=DEFAULT_INSTALL_ROOT,
        help=f"Zero Hour install root (default: {DEFAULT_INSTALL_ROOT})",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list-members", help="List combined archive members.")
    list_parser.add_argument("--pattern", help="Regex filter against logical member paths.")

    extract_parser = subparsers.add_parser("extract-member", help="Extract one logical member.")
    extract_parser.add_argument("member", help=r"Logical member path like Data\INI\Upgrade.ini")
    extract_parser.add_argument("--output", type=Path, help="Output file path. Defaults to stdout.")

    report_parser = subparsers.add_parser("gla-tech-report", help="Summarize GLA Palace, Black Market, and promotion data.")
    report_parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON.")

    inspect_parser = subparsers.add_parser("inspect-block", help="Inspect one named INI block.")
    inspect_parser.add_argument("file", help=r"Logical member path like Data\INI\CommandButton.ini")
    inspect_parser.add_argument("block_keyword", help="Block keyword, for example CommandButton or CommandSet")
    inspect_parser.add_argument("block_name", help="Exact block name to inspect")
    inspect_parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON.")

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    repo = IniDataRepository(args.install_root)

    if args.command == "list-members":
        for member in repo.list_members(args.pattern):
            print(member.member_path)
        return 0

    if args.command == "extract-member":
        data = repo.read_member_bytes(args.member)
        if args.output is None:
            sys.stdout.write(data.decode("latin-1"))
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(data)
            print(args.output)
        return 0

    if args.command == "gla-tech-report":
        report = build_gla_tech_report(repo)
        print(json.dumps(report, indent=2 if args.pretty else None))
        return 0

    if args.command == "inspect-block":
        report = inspect_named_block(repo, args.file, args.block_keyword, args.block_name)
        print(json.dumps(report, indent=2 if args.pretty else None))
        return 0

    parser.error(f"Unsupported command: {args.command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
