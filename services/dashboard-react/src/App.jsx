import { useEffect, useMemo, useState } from 'react';

const INGEST_HTTP = import.meta.env.VITE_INGEST_HTTP || 'http://localhost:8000';
const INGEST_WS = import.meta.env.VITE_INGEST_WS || 'ws://localhost:8000/ws/market';

export default function App() {
  const [latest, setLatest] = useState({});
  const [health, setHealth] = useState(null);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    const ws = new WebSocket(INGEST_WS);
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    ws.onmessage = (evt) => {
      const msg = JSON.parse(evt.data);
      if (!msg.symbol) return;
      setLatest((prev) => ({ ...prev, [msg.symbol]: msg }));
    };
    return () => ws.close();
  }, []);

  useEffect(() => {
    const fetchHealth = async () => {
      const resp = await fetch(`${INGEST_HTTP}/health`);
      const data = await resp.json();
      setHealth(data);
    };
    fetchHealth();
    const id = setInterval(fetchHealth, 1000);
    return () => clearInterval(id);
  }, []);

  const symbols = useMemo(() => Object.keys(latest).sort(), [latest]);

  return (
    <main className="page">
      <header className="hero">
        <h1>HFMM Live Monitor</h1>
        <p>Feed-event telemetry stream and ingest health.</p>
        <span className={connected ? 'pill on' : 'pill off'}>
          {connected ? 'stream connected' : 'stream disconnected'}
        </span>
      </header>

      <section className="grid two">
        <article className="card">
          <h2>Ingest Health</h2>
          {health ? (
            <ul>
              <li>queue_size: {health.queue_size}</li>
              <li>received: {health.received}</li>
              <li>inserted: {health.inserted}</li>
              <li>dropped: {health.dropped}</li>
            </ul>
          ) : (
            <p>Loading...</p>
          )}
        </article>

        <article className="card">
          <h2>Latest Events</h2>
          {symbols.length === 0 ? (
            <p>No symbols yet.</p>
          ) : (
            <table>
              <thead>
                <tr>
                  <th>symbol</th>
                  <th>kind</th>
                  <th>u</th>
                  <th>bids</th>
                  <th>asks</th>
                </tr>
              </thead>
              <tbody>
                {symbols.map((symbol) => {
                  const e = latest[symbol];
                  return (
                    <tr key={symbol}>
                      <td>{symbol}</td>
                      <td>{e.kind}</td>
                      <td>{e.final_update_id}</td>
                      <td>{e.bid_count}</td>
                      <td>{e.ask_count}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </article>
      </section>
    </main>
  );
}
