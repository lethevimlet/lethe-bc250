import { ButtonItem, Field, PanelSection, PanelSectionRow, ToggleField, staticClasses } from "@decky/ui";
import { callable, definePlugin, toaster } from "@decky/api";
import { useEffect, useState } from "react";
import { FaMoon } from "react-icons/fa";

declare const SteamClient: any;

type Settings = { pause_game: boolean; mute_audio: boolean; wake_on_input: boolean; hook_steam_sleep: boolean };
type Status = {
  asleep: boolean;
  since: number | null;
  frozen: number;
  game: { pid: number; appid: string } | null;
  settings: Settings;
  sleep_masked: boolean;
  user: string | null;
};
type Result = { ok: boolean; output: string };

const getStatus = callable<[], Status>("status");
const setSetting = callable<[key: string, value: boolean], Result>("set_setting");
const sleepNow = callable<[], Result>("sleep");
const wakeNow = callable<[], Result>("wake");

// Steam's own Sleep entry ends in SteamClient.System.SuspendPC(), which asks logind to suspend and
// hangs the BC-250. While the option is on we replace that function with our fake sleep (and the
// backend masks the systemd sleep units as a safety net, so a real suspend can never be reached).
// This Steam build has no RegisterForOnSuspendRequest, so wrapping the call is the only hook.
let originalSuspendPC: ((...a: any[]) => any) | null = null;
function installSuspendHook(on: boolean) {
  if (typeof SteamClient === "undefined" || !SteamClient?.System) return;
  if (on && !originalSuspendPC && typeof SteamClient.System.SuspendPC === "function") {
    originalSuspendPC = SteamClient.System.SuspendPC;
    SteamClient.System.SuspendPC = async (..._args: any[]) => {
      console.log("[bc250-sleep] Steam Sleep -> fake sleep");
      try { await sleepNow(); } catch (e) { console.error(e); }
    };
  } else if (!on && originalSuspendPC) {
    SteamClient.System.SuspendPC = originalSuspendPC;
    originalSuspendPC = null;
  }
}

function Content() {
  const [st, setSt] = useState<Status | null>(null);
  const [busy, setBusy] = useState(false);

  const refresh = async () => {
    try { setSt(await getStatus()); } catch (e) { console.error(e); }
  };
  useEffect(() => {
    refresh();
    const t = setInterval(refresh, 2000);
    return () => clearInterval(t);
  }, []);

  const act = async (label: string, fn: () => Promise<Result>) => {
    setBusy(true);
    try {
      const r = await fn();
      if (!r.ok) toaster.toast({ title: "BC-250 Sleep", body: `${label}: ${r.output}` });
    } catch (e) {
      toaster.toast({ title: "BC-250 Sleep", body: `${label} error: ${e}` });
    }
    setBusy(false);
    refresh();
  };
  const toggle = (key: keyof Settings) => async (v: boolean) => {
    await act(key, () => setSetting(key, v));
    if (key === "hook_steam_sleep") installSuspendHook(v);
  };

  if (!st) return <PanelSection><PanelSectionRow><Field label="Loading…" /></PanelSectionRow></PanelSection>;
  const s = st.settings;

  return (
    <>
      <PanelSection title="Sleep">
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy || st.asleep} onClick={() => act("sleep", sleepNow)}>
            Sleep now
          </ButtonItem>
        </PanelSectionRow>
        <PanelSectionRow>
          <Field label="State" focusable>
            {st.asleep ? `asleep, ${st.frozen} processes frozen` : st.game ? `awake, game running (app ${st.game.appid})` : "awake, no game running"}
          </Field>
        </PanelSectionRow>
        {st.asleep && (
          <PanelSectionRow>
            <ButtonItem layout="below" disabled={busy} onClick={() => act("wake", wakeNow)}>Wake</ButtonItem>
          </PanelSectionRow>
        )}
        <PanelSectionRow>
          <Field focusable description="The BC-250 cannot really sleep (no S3). This freezes the game, mutes audio and turns the TV off; any button press brings it back where you left it. The board stays on at its idle power." />
        </PanelSectionRow>
      </PanelSection>

      <PanelSection title="Options">
        <PanelSectionRow>
          <ToggleField label="Freeze the running game" description="SIGSTOP the game's processes; resume on wake" checked={s.pause_game} disabled={busy} onChange={toggle("pause_game")} />
        </PanelSectionRow>
        <PanelSectionRow>
          <ToggleField label="Mute audio" checked={s.mute_audio} disabled={busy} onChange={toggle("mute_audio")} />
        </PanelSectionRow>
        <PanelSectionRow>
          <ToggleField label="Wake on any input" description="Any controller, keyboard or mouse button press wakes" checked={s.wake_on_input} disabled={busy} onChange={toggle("wake_on_input")} />
        </PanelSectionRow>
        <PanelSectionRow>
          <ToggleField
            label="Use Steam's Sleep button"
            description={`Steam's Sleep runs this instead of the (broken) system suspend. Masks systemd sleep units. ${st.sleep_masked ? "Currently masked." : "Currently not masked."}`}
            checked={s.hook_steam_sleep}
            disabled={busy}
            onChange={toggle("hook_steam_sleep")}
          />
        </PanelSectionRow>
      </PanelSection>
    </>
  );
}

export default definePlugin(() => {
  getStatus().then((st) => installSuspendHook(!!st?.settings?.hook_steam_sleep)).catch(console.error);
  return {
    name: "BC-250 Sleep",
    titleView: <div className={staticClasses.Title}>BC-250 Sleep</div>,
    content: <Content />,
    icon: <FaMoon />,
    onDismount() { installSuspendHook(false); },
  };
});
