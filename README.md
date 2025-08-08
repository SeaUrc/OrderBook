# C++ Orderbook Simulation

A fully functional **limit order book** simulation in C++, supporting **Good-Till-Cancel (GTC)** and **Fill-And-Kill (FAK)** order types, **Buy** and **Sell** sides, and trade matching with a console  display.

## Features

- **Order Types**
    - `GoodTillCancel` (GTC): Remains in the book until explicitly cancelled or filled.
    - `FillAndKill` (FAK): Executes immediately if possible; otherwise discarded.

- **Order Matching**
    - Matches orders based on **price-time priority**.
    - Automatically executes trades when the highest bid ≥ lowest ask.

- **Order Management**
    - Add, cancel, and modify orders.
    - Track filled, remaining, and initial quantities.

- **Trade Recording**
    - Captures trade details: order IDs, price, and quantity for both sides.

- **Order Book Display**
    - Console output showing top N bid/ask levels.
    - Displays both price and aggregate quantity per price level.

- **Randomized Simulation**
    - Generates thousands of random orders with varying prices, quantities, types, and sides.
    - Prints trades and updates the order book display in real-time.

## Code Structure

| Class / Struct         | Purpose |
|------------------------|---------|
| `Order`                | Represents a single order with price, quantity, type, and side. |
| `OrderModify`          | Represents a modification request for an existing order. |
| `Trade` / `TradeInfo`  | Holds information for an executed trade between a bid and ask. |
| `Orderbook`            | Core matching engine — stores orders, executes trades, handles cancellations and modifications. |
| `OrderbookLevelInfos`  | Aggregates order levels for display. |
| `OrderbookPrinter`     | Formats and prints the order book to the terminal with colors. |
| `GenerateOrderId()`    | Produces unique incremental order IDs. |

## Matching Logic

- **Buy Order** matches if: buy_price >= lowest_ask
- **Sell Order** matches if: sell_price <= highest_bid
- Trades execute for the minimum of bid/ask remaining quantities.
- Fully filled orders are removed from the book.




