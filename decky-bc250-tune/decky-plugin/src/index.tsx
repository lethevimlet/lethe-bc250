import {
  ButtonItem,
  DropdownItem,
  Field,
  PanelSection,
  PanelSectionRow,
  ToggleField,
  staticClasses,
} from "@decky/ui";
import { callable, definePlugin, toaster } from "@decky/api";
import { useEffect, useState } from "react";
import { FaMicrochip } from "react-icons/fa";

// Shape of `bc250-tune status --json`
type Status = {
  version: string;
  user: string;
  uma: { config: number; cmos: number | null; live_mb: number };
  gpu: { min: number; max: number; config_min: number; config_max: number; cur_mhz: number; cur_mhz_estimated?: boolean; temp: number };
  cu: { config: number; live: number };
  cores: { config: number; mask: number | null; visible: number; auto_reboot?: string };
  hud: { config: string; installed: string };
  res: { config: string; session: string };
  power: { soc_w: number; total_w: number; cpu_temp: number };
  pending: { reboot: string; session_restart: number; cold_boot: number };
};
type SetResult = { ok: boolean; output: string };

const getStatus = callable<[], Status | null>("status");
const setOption = callable<[key: string, value: string], SetResult>("set_option");
const warmReboot = callable<[], SetResult>("reboot");
const restartSession = callable<[], SetResult>("restart_session");

const opt = (data: number | string, label: string) => ({ data, label });

function Content() {
  const [st, setSt] = useState<Status | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  const refresh = async () => {
    try {
      const s = await getStatus();
      if (s) { setSt(s); setErr(null); } else setErr("bc250-tune not installed or status failed");
    } catch (e) { setErr(String(e)); }
  };

  useEffect(() => {
    refresh();
    const t = setInterval(refresh, 3000);
    return () => clearInterval(t);
  }, []);

  const apply = async (key: string, value: string) => {
    setBusy(true);
    try {
      const r = await setOption(key, value);
      toaster.toast({
        title: "BC-250 Tune",
        body: r.ok ? `${key} = ${value} applied` : `failed: ${r.output.trim().split("\n").pop()}`,
      });
    } catch (e) {
      toaster.toast({ title: "BC-250 Tune", body: `error: ${e}` });
    }
    setBusy(false);
    refresh();
  };

  const runAction = async (label: string, fn: () => Promise<SetResult>) => {
    setBusy(true);
    try {
      const r = await fn();
      if (!r.ok) toaster.toast({ title: "BC-250 Tune", body: `${label} failed: ${r.output.trim().split("\n").pop()}` });
    } catch (e) {
      toaster.toast({ title: "BC-250 Tune", body: `${label} error: ${e}` });
    }
    setBusy(false);
  };

  if (!st) {
    return (
      <PanelSection>
        <PanelSectionRow><Field label={err ?? "Loading…"} /></PanelSectionRow>
      </PanelSection>
    );
  }

  const pending: string[] = [];
  if (st.pending.reboot) pending.push(`reboot: ${st.pending.reboot.replace(/;$/, "")}`);
  if (st.pending.session_restart) pending.push("restart gaming session (resolution)");
  if (st.pending.cold_boot) pending.push("cold boot (power off) to return to 6 cores");

  return (
    <>
      <PanelSection title="Live">
        <PanelSectionRow>
          <Field label="GPU" focusable>{st.gpu.cur_mhz_estimated ? "~" : ""}{st.gpu.cur_mhz} MHz · {st.gpu.temp}°C · {st.cu.live} CU</Field>
        </PanelSectionRow>
        <PanelSectionRow>
          <Field label="CPU" focusable>{st.cores.visible} cores · {st.power.cpu_temp}°C</Field>
        </PanelSectionRow>
        <PanelSectionRow>
          <Field label="Power" focusable>SoC {st.power.soc_w} W · total ~{st.power.total_w} W</Field>
        </PanelSectionRow>
        <PanelSectionRow>
          <Field label="VRAM" focusable>{st.uma.live_mb} MB{st.res.session ? ` · ${st.res.session}` : ""}</Field>
        </PanelSectionRow>
        {pending.map((p) => (
          <PanelSectionRow key={p}><Field label="Pending" focusable>{p}</Field></PanelSectionRow>
        ))}
      </PanelSection>

      <PanelSection title="GPU">
        <PanelSectionRow>
          <DropdownItem
            label="Compute units"
            description="Live. 32 = one extra WGP per shader row, a middle step if 40 misbehaves. 40: +~30 W, ~+5 % in games, 1.6x compute"
            rgOptions={[opt(24, "24 (stock)"), opt(32, "32"), opt(40, "40 (all WGPs)")]}
            selectedOption={st.cu.config}
            disabled={busy}
            onChange={(o) => apply("cu", String(o.data))}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <DropdownItem
            label="Max clock"
            description="Governor ceiling (live)"
            rgOptions={[opt(1500, "1500 MHz"), opt(1700, "1700 MHz"), opt(1850, "1850 MHz (default)"), opt(2000, "2000 MHz (hot)")]}
            selectedOption={st.gpu.config_max}
            disabled={busy}
            onChange={(o) => apply("gpu-max", String(o.data))}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <DropdownItem
            label="Idle clock"
            description="Governor floor (live). 500 saves ~10 W at idle"
            rgOptions={[opt(500, "500 MHz"), opt(1000, "1000 MHz")]}
            selectedOption={st.gpu.config_min}
            disabled={busy}
            onChange={(o) => apply("gpu-min", String(o.data))}
          />
        </PanelSectionRow>
      </PanelSection>

      <PanelSection title="CPU">
        <PanelSectionRow>
          <ToggleField
            label="8 cores"
            description="SMU core unlock. Needs a warm reboot; a cold boot (power off) reverts to 6"
            checked={st.cores.config === 8}
            disabled={busy}
            onChange={(v) => apply("cores", v ? "8" : "6")}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <ToggleField
            label="Auto warm reboot after power-on"
            description="A cold boot always comes up with 6 cores; with this on, the boot service reboots once by itself (adds ~40 s)"
            checked={st.cores.auto_reboot === "on"}
            disabled={busy}
            onChange={(v) => apply("cores-auto-reboot", v ? "on" : "off")}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={() => runAction("reboot", warmReboot)}>
            Warm reboot now
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>

      <PanelSection title="Memory">
        <PanelSectionRow>
          <DropdownItem
            label="VRAM (UMA split)"
            description="Written to CMOS, applies at next reboot. Rest of the 16 GB is system RAM"
            rgOptions={[512, 1024, 2048, 3072, 4096, 6144, 8192, 10240, 12288].map((m) => opt(m, `${m} MB`))}
            selectedOption={st.uma.config}
            disabled={busy}
            onChange={(o) => apply("uma", String(o.data))}
          />
        </PanelSectionRow>
      </PanelSection>

      <PanelSection title="Display & HUD">
        <PanelSectionRow>
          <ToggleField
            label="Custom HUD line"
            description="Installs the one-line MangoHud layout as Performance Overlay level 1"
            checked={st.hud.config === "on"}
            disabled={busy}
            onChange={(v) => apply("hud", v ? "on" : "off")}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <DropdownItem
            label="Gaming Mode resolution"
            description="Applies when the gaming session restarts"
            rgOptions={[opt("1920x1080", "1080p"), opt("2560x1440", "1440p"), opt("3840x2160", "4K"), opt("native", "native")]}
            selectedOption={st.res.config}
            disabled={busy}
            onChange={(o) => apply("res", String(o.data))}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={() => runAction("session restart", restartSession)}>
            Restart gaming session (closes game)
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>
    </>
  );
}

export default definePlugin(() => ({
  name: "BC-250 Tune",
  titleView: <div className={staticClasses.Title}>BC-250 Tune</div>,
  content: <Content />,
  icon: <FaMicrochip />,
  onDismount() {},
}));
