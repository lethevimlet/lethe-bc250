// BC-250 Input: Quick Access panel. Status of the phone link, pairing QR code, mouse control and
// typing settings; everything is read from and written through main.py.
import { ButtonItem, DropdownItem, Field, PanelSection, PanelSectionRow, SliderField, ToggleField, staticClasses } from "@decky/ui";
import { callable, definePlugin } from "@decky/api";
import { useEffect, useMemo, useRef, useState } from "react";
import { FaKeyboard } from "react-icons/fa";
import { renderSVG } from "uqr";

type Ip = { iface: string; ip: string; kind: string };
type Client = { ip: string; ago: number; live: boolean };
type LastSend = {
  time: number; chars: number; preview: string; replaced: number;
  ok: boolean; error: string | null; duration: number;
};
type Status = {
  enabled: boolean; running: boolean; serverError: string | null;
  uinputOk: boolean; uinputError: string | null;
  pointerEnabled: boolean; mouseOk: boolean; mouseError: string | null; clients: Client[];
  ip: string; ips: Ip[]; port: number; url: string;
  openKey: string; keyDelayMs: number; openDelayMs: number;
  sends: number; lastSend: LastSend | null;
};

const getStatus = callable<[], Status>("get_status");
const setEnabled = callable<[boolean], Status>("set_enabled");
const setSetting = callable<[string, unknown], Status>("set_setting");
const retryUinput = callable<[], Status>("retry_uinput");

const OPEN_KEY_OPTIONS = [
  { data: "enter", label: "Enter (WoW default)" },
  { data: "slash", label: "/ (only for /commands)" },
  { data: "none", label: "None (chat already open)" },
];

function ago(ts: number): string {
  const s = Math.max(0, Math.round(Date.now() / 1000 - ts));
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.round(s / 60)}m ago`;
  return `${Math.round(s / 3600)}h ago`;
}

function QrCode({ text }: { text: string }) {
  const src = useMemo(() => {
    const svg = renderSVG(text, { border: 2, pixelSize: 6, ecc: "M" });
    return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg);
  }, [text]);
  return (
    <div style={{ display: "flex", justifyContent: "center", padding: "6px 0" }}>
      <img src={src} style={{ width: 200, height: 200, imageRendering: "pixelated", borderRadius: 6 }} />
    </div>
  );
}

function phoneLine(st: Status): { label: string; description: string } {
  if (!st.running) return { label: "Phone link off", description: st.serverError ?? "Turn on Phone input to pair a phone" };
  if (!st.clients.length) return { label: "No phone connected", description: "Scan the QR code below with your phone" };
  const live = st.clients.filter((c) => c.live);
  const label = live.length ? (live.length > 1 ? `${live.length} phones connected` : "Phone connected") : "Phone seen recently";
  const description = st.clients
    .map((c) => `${c.ip} · ${c.live ? "live" : `${c.ago < 5 ? "just now" : ago(Date.now() / 1000 - c.ago)}`}`)
    .join("\n");
  return { label, description };
}

function Content() {
  const [st, setSt] = useState<Status | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [delay, setDelay] = useState<number | null>(null);
  const [showQr, setShowQr] = useState<boolean | null>(null);
  const delayTimer = useRef<number | undefined>(undefined);

  const refresh = async () => {
    try {
      setSt(await getStatus());
      setErr(null);
    } catch (e) {
      setErr(String(e));
    }
  };

  useEffect(() => {
    refresh();
    const id = window.setInterval(refresh, 2000);
    return () => window.clearInterval(id);
  }, []);

  if (!st) {
    return (
      <PanelSection>
        <PanelSectionRow>
          <Field label={err ? "Backend error" : "Loading..."} description={err ?? undefined} />
        </PanelSectionRow>
      </PanelSection>
    );
  }

  const others = st.ips.filter((i) => i.ip !== st.ip);
  const last = st.lastSend;
  const phone = phoneLine(st);
  // The QR code is big: show it until a phone has paired, then on request.
  const qrVisible = showQr ?? st.clients.length === 0;
  const devError = !st.uinputOk ? st.uinputError ?? "Virtual keyboard not created yet"
    : st.pointerEnabled && !st.mouseOk ? st.mouseError ?? "Virtual mouse not created yet" : null;

  return (
    <>
      <PanelSection>
        <PanelSectionRow>
          <ToggleField
            label="Phone input"
            description={st.running ? `Listening on ${st.ip}:${st.port}` : st.serverError ?? "Off"}
            checked={st.enabled}
            onChange={async (v) => setSt(await setEnabled(v))}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <Field
            label={phone.label}
            bottomSeparator="none"
            description={<span style={{ whiteSpace: "pre-line" }}>{phone.description}</span>}
          />
        </PanelSectionRow>
        {devError && (
          <PanelSectionRow>
            <ButtonItem layout="below" description={devError} onClick={async () => setSt(await retryUinput())}>
              Retry virtual devices
            </ButtonItem>
          </PanelSectionRow>
        )}
      </PanelSection>

      {st.running && (
        <PanelSection title="Connect a phone">
          {qrVisible && (
            <PanelSectionRow>
              <QrCode text={st.url} />
            </PanelSectionRow>
          )}
          {qrVisible && (
            <PanelSectionRow>
              <Field
                label={`${st.ip}:${st.port}`}
                bottomSeparator="none"
                description={
                  <span style={{ wordBreak: "break-all", fontSize: 11 }}>
                    {st.url}
                    {others.length > 0 && (
                      <>
                        <br />
                        Also: {others.map((o) => `${o.ip} (${o.kind})`).join(", ")}
                      </>
                    )}
                  </span>
                }
              />
            </PanelSectionRow>
          )}
          <PanelSectionRow>
            <ButtonItem layout="below" onClick={() => setShowQr(!qrVisible)}>
              {qrVisible ? "Hide QR code" : "Show QR code"}
            </ButtonItem>
          </PanelSectionRow>
        </PanelSection>
      )}

      <PanelSection title="Touchpad">
        <PanelSectionRow>
          <ToggleField
            label="Mouse control"
            description="The phone's touchpad moves the pointer, clicks and scrolls"
            checked={st.pointerEnabled}
            onChange={async (v) => setSt(await setSetting("pointer_enabled", v))}
          />
        </PanelSectionRow>
      </PanelSection>

      <PanelSection title="Typing">
        <PanelSectionRow>
          <DropdownItem
            label="Open chat with"
            rgOptions={OPEN_KEY_OPTIONS}
            selectedOption={st.openKey}
            onChange={async (o) => setSt(await setSetting("open_key", o.data))}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <SliderField
            label="Key delay"
            description="Raise this if letters get dropped"
            value={delay ?? st.keyDelayMs}
            min={2}
            max={60}
            step={1}
            showValue
            valueSuffix=" ms"
            onChange={(v) => {
              setDelay(v);
              window.clearTimeout(delayTimer.current);
              delayTimer.current = window.setTimeout(async () => {
                setSt(await setSetting("key_delay_ms", v));
                setDelay(null);
              }, 400);
            }}
          />
        </PanelSectionRow>
        <PanelSectionRow>
          <SliderField
            label="Wait after opening chat"
            value={st.openDelayMs}
            min={0}
            max={500}
            step={20}
            showValue
            valueSuffix=" ms"
            onChange={async (v) => setSt(await setSetting("open_delay_ms", v))}
          />
        </PanelSectionRow>
      </PanelSection>

      <PanelSection title="Last message">
        <PanelSectionRow>
          <Field
            label={last ? (last.ok ? `Typed ${last.chars} chars` : "Failed") : "Nothing yet"}
            description={
              last
                ? `${ago(last.time)}${last.ok ? ` in ${last.duration}s` : ""}: ${last.ok ? last.preview : last.error}` +
                  (last.replaced ? ` (${last.replaced} replaced with ?)` : "")
                : "Close this menu before sending; keys go to whatever has focus."
            }
          />
        </PanelSectionRow>
      </PanelSection>
    </>
  );
}

export default definePlugin(() => ({
  name: "BC-250 Input",
  titleView: <div className={staticClasses.Title}>BC-250 Input</div>,
  content: <Content />,
  icon: <FaKeyboard />,
  onDismount() {},
}));
