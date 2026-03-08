ROOT_DIR="/home/bguo/work_learn_za/quant/execution/hf_marketmaking"
cd $ROOT_DIR
cd services/ingest-fastapi
uvicorn app.main:app --host 0.0.0.0 --port 8000  &
cd $ROOT_DIR
cd services/dashboard-react
npm run dev  &
cd $ROOT_DIR
./build/hfmm
