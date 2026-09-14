#!/usr/bin/env python3
"""
pdheadtui - TUI for inspecting and editing PD head model files.

Calls pdheadedit (C CLI) as a subprocess for parsing/serialization.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

from textual import on, work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    Select,
    Static,
    Tree,
)
from textual.widgets.tree import TreeNode

# ── Locate pdheadedit binary ─────────────────────────────────────

# lib/pdheadtui/ is sibling to lib/pdheadedit/ in the tool root.
_LIB_DIR = Path(__file__).resolve().parent.parent
PDHEADEDIT = _LIB_DIR / "pdheadedit" / "pdheadedit"

if not PDHEADEDIT.exists():
    # Fallback: fojo layout (tools/pdheadtui/../pdheadedit)
    alt = Path(__file__).resolve().parent.parent / "pdheadedit" / "pdheadedit"
    if alt.exists():
        PDHEADEDIT = alt
    # Fallback: on PATH
    import shutil
    p = shutil.which("pdheadedit")
    if p:
        PDHEADEDIT = Path(p)

# ── Subprocess helpers ───────────────────────────────────────────


def run_inspect(filepath: str) -> dict:
    result = subprocess.run(
        [str(PDHEADEDIT), "inspect", filepath],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"pdheadedit inspect failed:\n{result.stderr}")
    return json.loads(result.stdout)


def run_validate(filepath: str) -> dict:
    result = subprocess.run(
        [str(PDHEADEDIT), "validate", filepath],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"pdheadedit validate failed:\n{result.stderr}")
    return json.loads(result.stdout)


def run_edit(filepath: str, output_path: str, ops: dict) -> dict:
    result = subprocess.run(
        [str(PDHEADEDIT), "edit", filepath, output_path],
        input=json.dumps(ops),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        # Try to get error from stdout JSON
        try:
            return json.loads(result.stdout)
        except Exception:
            raise RuntimeError(
                f"pdheadedit edit failed:\n{result.stderr}\n{result.stdout}"
            )
    return json.loads(result.stdout)


# ── Data formatting ──────────────────────────────────────────────

PART_NAMES = {0: "SUNGLASSES", 1: "HAT", 2: "EYESOPEN", 3: "EYESCLOSED", 4: "HUDPIECE"}


def node_label(node: dict) -> str:
    """Build a concise label for a tree node."""
    ntype = node.get("type", "?")
    nid = node.get("id", "?")
    parts = []
    parts.append(f"[bold]{ntype}[/bold] #{nid}")

    if node.get("part_name"):
        parts.append(f"[cyan]{node['part_name']}[/cyan]")

    rd = node.get("rodata")
    if rd and ntype == "DL":
        parts.append(f"{rd.get('numvertices', '?')}v")
    elif rd and ntype == "TOGGLE":
        tgt = rd.get("target_node_id", -1)
        parts.append(f"→#{tgt}")
    elif rd and ntype == "BBOX":
        hp = rd.get("hitpart", "?")
        parts.append(f"hitpart={hp}")

    return " ".join(parts)


def format_rodata(node: dict) -> str:
    """Format rodata as multiline detail text."""
    rd = node.get("rodata")
    if not rd:
        return "(no rodata)"
    lines = []
    for k, v in rd.items():
        if isinstance(v, float):
            lines.append(f"  {k}: {v:.4f}")
        else:
            lines.append(f"  {k}: {v}")
    return "\n".join(lines)


# ── Modal: Add Toggle ────────────────────────────────────────────


class AddToggleModal(ModalScreen[dict | None]):
    """Modal dialog for the add_toggle operation."""

    BINDINGS = [Binding("escape", "cancel", "Cancel")]

    def __init__(self, max_node_id: int) -> None:
        super().__init__()
        self.max_node_id = max_node_id

    def compose(self) -> ComposeResult:
        with Vertical(id="modal-dialog"):
            yield Label("Add Toggle Node", id="modal-title")
            yield Label("Part name:")
            yield Select(
                [(name, name) for name in PART_NAMES.values()],
                id="part-select",
                prompt="Select part...",
            )
            yield Label(f"Child node ID (0-{self.max_node_id}):")
            yield Input(id="child-node-input", placeholder="node id")
            with Horizontal(id="modal-buttons"):
                yield Button("OK", variant="primary", id="ok-btn")
                yield Button("Cancel", id="cancel-btn")

    @on(Button.Pressed, "#ok-btn")
    def on_ok(self) -> None:
        part_sel = self.query_one("#part-select", Select)
        child_input = self.query_one("#child-node-input", Input)
        if part_sel.value is Select.BLANK:
            self.notify("Select a part name", severity="error")
            return
        try:
            child_id = int(child_input.value)
        except ValueError:
            self.notify("Enter a valid node ID", severity="error")
            return
        self.dismiss(
            {"op": "add_toggle", "part_name": part_sel.value, "child_node_id": child_id}
        )

    @on(Button.Pressed, "#cancel-btn")
    def on_cancel(self) -> None:
        self.dismiss(None)

    def action_cancel(self) -> None:
        self.dismiss(None)


# ── Modal: Rebind Texture ────────────────────────────────────────


class RebindTextureModal(ModalScreen[dict | None]):
    """Modal dialog for the rebind_texture operation."""

    BINDINGS = [Binding("escape", "cancel", "Cancel")]

    def __init__(self, node_id: int, num_texconfigs: int) -> None:
        super().__init__()
        self._node_id = node_id
        self._num_texconfigs = num_texconfigs

    def compose(self) -> ComposeResult:
        with Vertical(id="modal-dialog"):
            yield Label(f"Rebind Texture (node #{self._node_id})", id="modal-title")
            yield Label(f"Texture config index (0-{self._num_texconfigs - 1}):")
            yield Input(id="texconfig-input", placeholder="texconfig index")
            with Horizontal(id="modal-buttons"):
                yield Button("OK", variant="primary", id="ok-btn")
                yield Button("Cancel", id="cancel-btn")

    @on(Button.Pressed, "#ok-btn")
    def on_ok(self) -> None:
        tc_input = self.query_one("#texconfig-input", Input)
        try:
            tc_idx = int(tc_input.value)
        except ValueError:
            self.notify("Enter a valid index", severity="error")
            return
        if tc_idx < 0 or tc_idx >= self._num_texconfigs:
            self.notify(f"Index must be 0-{self._num_texconfigs - 1}", severity="error")
            return
        self.dismiss(
            {"op": "rebind_texture", "node_id": self._node_id, "texconfig_index": tc_idx}
        )

    @on(Button.Pressed, "#cancel-btn")
    def on_cancel(self) -> None:
        self.dismiss(None)

    def action_cancel(self) -> None:
        self.dismiss(None)


# ── Main App ─────────────────────────────────────────────────────


class PDHeadTUI(App):
    """PD Head Model Inspector / Editor"""

    CSS = """
    #main-container {
        layout: horizontal;
    }
    #left-pane {
        width: 1fr;
        min-width: 40;
        border-right: solid $accent;
    }
    #right-pane {
        width: 1fr;
        min-width: 50;
        padding: 1 2;
    }
    #detail-box {
        height: 1fr;
        overflow-y: auto;
    }
    #texconfig-table {
        height: auto;
        max-height: 16;
    }
    #model-info {
        height: auto;
        padding: 0 0 1 0;
    }
    #ops-log {
        height: auto;
        max-height: 6;
        border-top: solid $accent;
        padding: 1;
    }
    #modal-dialog {
        width: 50;
        height: auto;
        padding: 1 2;
        background: $surface;
        border: thick $accent;
        align: center middle;
    }
    #modal-title {
        text-style: bold;
        padding-bottom: 1;
    }
    #modal-buttons {
        padding-top: 1;
        align: right middle;
    }
    #modal-buttons Button {
        margin-left: 1;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "Quit"),
        Binding("i", "show_info", "Info"),
        Binding("v", "validate", "Validate"),
        Binding("t", "add_toggle", "Add Toggle"),
        Binding("r", "rebind_tex", "Rebind Tex"),
        Binding("w", "write_file", "Write"),
    ]

    TITLE = "pdheadtui"
    SUB_TITLE = "PD Head Model Inspector"

    def __init__(self, filepath: str) -> None:
        super().__init__()
        self.filepath = filepath
        self.model_data: dict = {}
        self.pending_ops: list[dict] = []
        self._node_map: dict[int, dict] = {}  # node id -> node dict

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal(id="main-container"):
            with Vertical(id="left-pane"):
                yield Tree("Model", id="model-tree")
            with Vertical(id="right-pane"):
                yield Static("", id="model-info")
                yield Static("", id="detail-box")
                yield DataTable(id="texconfig-table")
                yield Static("", id="ops-log")
        yield Footer()

    def on_mount(self) -> None:
        self.load_model()

    @work(thread=True)
    def load_model(self) -> None:
        try:
            data = run_inspect(self.filepath)
        except Exception as e:
            self.notify(str(e), severity="error", timeout=10)
            return
        self.call_from_thread(self._populate_ui, data)

    def _populate_ui(self, data: dict) -> None:
        self.model_data = data
        self._node_map.clear()
        self._index_nodes(data.get("root"))

        # Update subtitle
        self.sub_title = data.get("file", "")

        # Model info
        md = data.get("modeldef", {})
        info_lines = [
            f"[bold]File:[/bold] {data.get('file', '?')}",
            f"[bold]Skel:[/bold] {md.get('skel_name', '?')} ({md.get('skel', '?')})",
            f"[bold]Scale:[/bold] {md.get('scale', '?'):.4f}",
            f"[bold]Parts:[/bold] {md.get('numparts', '?')}  "
            f"[bold]Matrices:[/bold] {md.get('nummatrices', '?')}  "
            f"[bold]Textures:[/bold] {md.get('numtexconfigs', '?')}",
        ]
        self.query_one("#model-info", Static).update("\n".join(info_lines))

        # Build tree
        tree = self.query_one("#model-tree", Tree)
        tree.clear()
        tree.root.set_label(f"[bold]{data.get('file', 'Model')}[/bold]")
        root_node = data.get("root")
        if root_node:
            self._add_tree_node(tree.root, root_node)
        tree.root.expand_all()

        # Texconfig table
        table = self.query_one("#texconfig-table", DataTable)
        table.clear(columns=True)
        table.add_columns("Idx", "WxH", "Format", "Depth", "Embedded", "Ptr/Len")
        for tc in data.get("texconfigs", []):
            table.add_row(
                str(tc["index"]),
                f"{tc['width']}×{tc['height']}",
                tc.get("format", "?"),
                str(tc.get("depth", "?")),
                "Yes" if tc.get("embedded") else "No",
                str(tc.get("texdata_len", 0)) if tc.get("embedded") else f"0x{tc.get('ptr_raw', 0):x}",
            )

        self._update_ops_log()

    def _index_nodes(self, node: dict | None) -> None:
        if node is None:
            return
        self._node_map[node["id"]] = node
        for child in node.get("children", []):
            self._index_nodes(child)

    def _add_tree_node(self, parent: TreeNode, node: dict) -> None:
        label = node_label(node)
        children = node.get("children", [])
        tree_node = parent.add(label, data=node, expand=True)
        for child in children:
            self._add_tree_node(tree_node, child)

    @on(Tree.NodeHighlighted, "#model-tree")
    def on_tree_highlight(self, event: Tree.NodeHighlighted) -> None:
        node = event.node.data
        if not isinstance(node, dict):
            return
        lines = [
            f"[bold]{node.get('type', '?')}[/bold] node #{node.get('id', '?')}",
        ]
        if node.get("part_name"):
            lines.append(f"Part: [cyan]{node['part_name']}[/cyan] (#{node.get('part_num', '?')})")
        lines.append("")
        lines.append("[bold]Rodata:[/bold]")
        lines.append(format_rodata(node))
        self.query_one("#detail-box", Static).update("\n".join(lines))

    def _update_ops_log(self) -> None:
        if not self.pending_ops:
            self.query_one("#ops-log", Static).update("[dim]No pending edits[/dim]")
        else:
            lines = [f"[bold]Pending edits ({len(self.pending_ops)}):[/bold]"]
            for i, op in enumerate(self.pending_ops):
                lines.append(f"  {i + 1}. {op.get('op', '?')}: {op}")
            self.query_one("#ops-log", Static).update("\n".join(lines))

    # ── Actions ─────────────────────────────────────

    def action_show_info(self) -> None:
        md = self.model_data.get("modeldef", {})
        parts = self.model_data.get("parts", [])
        lines = [
            f"File: {self.model_data.get('file', '?')}",
            f"Skel: {md.get('skel_name', '?')} ({md.get('skel', '?')})",
            f"Scale: {md.get('scale', '?')}",
            f"Nodes: {len(self._node_map)}",
            f"Parts: {len(parts)}",
            f"Texconfigs: {md.get('numtexconfigs', '?')}",
            "",
            "Parts:",
        ]
        for p in parts:
            lines.append(f"  #{p['part_num']} {p.get('part_name', '?')} → node #{p['node_id']}")
        self.notify("\n".join(lines), timeout=10)

    @work(thread=True)
    def action_validate(self) -> None:
        try:
            v = run_validate(self.filepath)
        except Exception as e:
            self.notify(str(e), severity="error", timeout=10)
            return
        lines = [
            f"skel_head: {v.get('skel_head')}",
            f"nodes: {v.get('num_nodes')}  DL: {v.get('num_dl_nodes')}  toggle: {v.get('num_toggle_nodes')}",
            f"vertices: {v.get('total_vertices')}  texconfigs: {v.get('num_texconfigs')}",
        ]
        parts = v.get("parts", {})
        for name, info in parts.items():
            status = "✓" if info.get("present") else "✗"
            lines.append(f"  {status} {name}")
        self.notify("\n".join(lines), title="Validation", timeout=10)

    def action_add_toggle(self) -> None:
        max_id = max(self._node_map.keys()) if self._node_map else 0
        self.push_screen(AddToggleModal(max_id), callback=self._on_add_toggle_result)

    def _on_add_toggle_result(self, result: dict | None) -> None:
        if result is None:
            return
        self.pending_ops.append(result)
        self._update_ops_log()
        self.notify(f"Queued: add_toggle {result.get('part_name')} → #{result.get('child_node_id')}")

    def action_rebind_tex(self) -> None:
        # Use currently highlighted node
        tree = self.query_one("#model-tree", Tree)
        current = tree.cursor_node
        if not current or not isinstance(current.data, dict):
            self.notify("Select a DL node first", severity="warning")
            return
        node = current.data
        if node.get("type") != "DL":
            self.notify("Select a DL node for texture rebind", severity="warning")
            return
        num_tc = self.model_data.get("modeldef", {}).get("numtexconfigs", 0)
        if num_tc == 0:
            self.notify("No texconfigs in model", severity="error")
            return
        self.push_screen(
            RebindTextureModal(node["id"], num_tc),
            callback=self._on_rebind_tex_result,
        )

    def _on_rebind_tex_result(self, result: dict | None) -> None:
        if result is None:
            return
        self.pending_ops.append(result)
        self._update_ops_log()
        self.notify(
            f"Queued: rebind_texture node #{result.get('node_id')} → tc #{result.get('texconfig_index')}"
        )

    def action_write_file(self) -> None:
        if not self.pending_ops:
            self.notify("No pending edits to write", severity="warning")
            return
        # Generate output path: append .edited before extension
        base = Path(self.filepath)
        out = base.parent / (base.stem + ".edited" + base.suffix)
        self._do_write(str(out))

    @work(thread=True)
    def _do_write(self, output_path: str) -> None:
        try:
            result = run_edit(self.filepath, output_path, {"ops": self.pending_ops})
        except Exception as e:
            self.notify(str(e), severity="error", timeout=10)
            return

        status = result.get("status", "?")
        if status == "ok":
            self.call_from_thread(self._on_write_ok, output_path, result)
        else:
            error = result.get("error", "unknown error")
            self.notify(f"Edit failed: {error}", severity="error", timeout=10)

    def _on_write_ok(self, output_path: str, result: dict) -> None:
        self.pending_ops.clear()
        self._update_ops_log()
        self.notify(f"Written: {output_path}", timeout=10)
        # Reload from the new file to show updated model
        self.filepath = output_path
        self.load_model()


# ── Entry point ──────────────────────────────────────────────────


def main() -> None:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <head-file.Z>", file=sys.stderr)
        print("\nInspect and edit PD head model files.", file=sys.stderr)
        sys.exit(1)

    filepath = sys.argv[1]
    if not os.path.isfile(filepath):
        print(f"error: file not found: {filepath}", file=sys.stderr)
        sys.exit(1)

    if not PDHEADEDIT.exists():
        print(f"error: pdheadedit not found at {PDHEADEDIT}", file=sys.stderr)
        print("Build it first: cd tools/pdheadedit && make", file=sys.stderr)
        sys.exit(1)

    app = PDHeadTUI(filepath)
    app.run()


if __name__ == "__main__":
    main()
