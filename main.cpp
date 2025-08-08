#include <iostream>
#include <map>
#include <list>
#include <string>
#include <vector>
#include <numeric>
#include <memory>
#include <stdexcept>
#include <format>
#include <unordered_map>
#include <chrono>
#include <random>
#include <thread>
#include <iomanip>
#include <sstream>

enum class OrderType { GoodTillCancel, FillAndKill };
enum class Side { Buy, Sell };

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

struct LevelInfo {
    Price price;
    Quantity quantity;
};

OrderId GenerateOrderId() {
    static OrderId currentId = 1000;
    return currentId++;
}

using LevelInfos = std::vector<LevelInfo>;
using OrderPointer = std::shared_ptr<class Order>;
using OrderPointers = std::list<OrderPointer>;

class Order {
public:
    Order(OrderType type, OrderId id, Side side, Price price, Quantity qty)
            : type(type), id(id), side(side), price(price), initialQty(qty), remainingQty(qty) {}

    OrderId GetId() const { return id; }
    Side GetSide() const { return side; }
    Price GetPrice() const { return price; }
    OrderType GetType() const { return type; }
    Quantity GetInitialQty() const { return initialQty; }
    Quantity GetRemainingQty() const { return remainingQty; }
    Quantity GetFilledQty() const { return initialQty - remainingQty; }
    bool IsFilled() const { return remainingQty == 0; }

    void Fill(Quantity qty) {
        if (qty > remainingQty)
            throw std::logic_error("Order (" + std::to_string(id) + ") overfill.");
        remainingQty -= qty;
    }

private:
    OrderType type;
    OrderId id;
    Side side;
    Price price;
    Quantity initialQty;
    Quantity remainingQty;
};

class OrderModify {
public:
    OrderModify(OrderId id, Side side, Price price, Quantity qty)
            : id(id), side(side), price(price), qty(qty) {}

    OrderId GetId() const { return id; }
    Price GetPrice() const { return price; }
    Side GetSide() const { return side; }
    Quantity GetQty() const { return qty; }

    OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, id, side, price, qty);
    }

private:
    OrderId id;
    Side side;
    Price price;
    Quantity qty;
};

struct TradeInfo {
    OrderId id;
    Price price;
    Quantity qty;
};

class Trade {
public:
    Trade(const TradeInfo& bid, const TradeInfo& ask) : bid(bid), ask(ask) {}
    const TradeInfo& GetBid() const { return bid; }
    const TradeInfo& GetAsk() const { return ask; }

private:
    TradeInfo bid;
    TradeInfo ask;
};

using Trades = std::vector<Trade>;

class OrderbookLevelInfos {
public:
    OrderbookLevelInfos(const LevelInfos& bids, const LevelInfos& asks) : bids(bids), asks(asks) {}
    const LevelInfos& GetBids() const { return bids; }
    const LevelInfos& GetAsks() const { return asks; }

private:
    LevelInfos bids;
    LevelInfos asks;
};

class Orderbook {
private:
    struct OrderEntry {
        OrderPointer order;
        OrderPointers::iterator location;
    };

    std::map<Price, OrderPointers, std::greater<>> bids;
    std::map<Price, OrderPointers, std::less<>> asks;
    std::unordered_map<OrderId, OrderEntry> orders;

    bool CanMatch(Side side, Price price) const {
        if (side == Side::Buy && !asks.empty())
            return price >= asks.begin()->first;
        if (side == Side::Sell && !bids.empty())
            return price <= bids.begin()->first;
        return false;
    }

    Trades MatchOrders() {
        Trades trades;

        while (!bids.empty() && !asks.empty()) {
            auto bidIt = bids.begin();
            auto askIt = asks.begin();
            if (bidIt->first < askIt->first) break;

            auto& bidList = bidIt->second;
            auto& askList = askIt->second;

            while (!bidList.empty() && !askList.empty()) {
                auto& bid = bidList.front();
                auto& ask = askList.front();

                Quantity qty = std::min(bid->GetRemainingQty(), ask->GetRemainingQty());
                bid->Fill(qty);
                ask->Fill(qty);

                trades.emplace_back(Trade{
                        {bid->GetId(), bid->GetPrice(), qty},
                        {ask->GetId(), ask->GetPrice(), qty}
                });

                if (bid->IsFilled()) {
                    orders.erase(bid->GetId());
                    bidList.pop_front();
                }
                if (ask->IsFilled()) {
                    orders.erase(ask->GetId());
                    askList.pop_front();
                }
            }

            if (bidList.empty()) bids.erase(bidIt);
            if (askList.empty()) asks.erase(askIt);
        }
        return trades;
    }

public:
    Trades AddOrder(OrderPointer order) {
        if (orders.find(order->GetId()) == orders.end()) return {};

        if (order->GetType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
            return {};

        if (order->GetSide() == Side::Buy) {
            auto& list = bids[order->GetPrice()];
            list.push_back(order);
            auto it = std::prev(list.end());
            orders[order->GetId()] = {order, it};
        } else {
            auto& list = asks[order->GetPrice()];
            list.push_back(order);
            auto it = std::prev(list.end());
            orders[order->GetId()] = {order, it};
        }


        return MatchOrders();
    }

    void CancelOrder(OrderId id) {
        auto it = orders.find(id);
        if (it == orders.end()) return;

        OrderPointer order = it->second.order;
        OrderPointers::iterator listIt = it->second.location;
        Price price = order->GetPrice();

        if (order->GetSide() == Side::Buy) {
            auto& list = bids.at(price);
            list.erase(listIt);
            if (list.empty()) bids.erase(price);
        } else {
            auto& list = asks.at(price);
            list.erase(listIt);
            if (list.empty()) asks.erase(price);
        }

        orders.erase(it);
    }


    Trades ModifyOrder(OrderModify mod) {
        if (orders.find(mod.GetId()) == orders.end()) return {};

        auto oldType = orders[mod.GetId()].order->GetType();
        CancelOrder(mod.GetId());
        return AddOrder(mod.ToOrderPointer(oldType));
    }

    std::size_t Size() const { return orders.size(); }

    OrderbookLevelInfos GetOrderInfos() const {
        LevelInfos bidLevels, askLevels;

        auto createLevel = [](Price p, const OrderPointers& orders) {
            return LevelInfo{p, std::accumulate(orders.begin(), orders.end(), Quantity(0),
                                                [](Quantity sum, const OrderPointer& o) { return sum + o->GetRemainingQty(); })};
        };

        for (const auto& [p, o] : bids) bidLevels.push_back(createLevel(p, o));
        for (const auto& [p, o] : asks) askLevels.push_back(createLevel(p, o));

        return OrderbookLevelInfos(bidLevels, askLevels);
    }
};

class OrderbookPrinter {
public:
    static void Print(const OrderbookLevelInfos& info, std::size_t depth = 6) {
        auto bids = info.GetBids();
        auto asks = info.GetAsks();

        std::cout << "\033[2J\033[1;1H"
                  << "\033[33m\n┌─────────────┬─────────────┐\n"
                  << "│  \033[1mBIDS (BUY)\033[0;33m │ \033[1mASKS (SELL)\033[0;33m │\n"
                  << "├──────┬──────┼──────┬──────┤\033[0m\n";

        for (size_t i = 0; i < depth; ++i) {
            std::ostringstream bidStr, askStr;

            if (i < bids.size())
                bidStr << "\033[32m" << std::setw(6) << bids[i].price << "│" << std::setw(6) << bids[i].quantity << "\033[0m";
            else
                bidStr << "      │      ";

            if (i < asks.size())
                askStr << "\033[31m" << std::setw(6) << asks[i].price << "│" << std::setw(6) << asks[i].quantity << "\033[0m";
            else
                askStr << "      │      ";

            std::cout << "│" << bidStr.str() << "│" << askStr.str() << "│\n";
        }

        std::cout << "\033[33m└──────┴──────┴──────┴──────┘\033[0m\n";
    }
};

int main() {
    Orderbook orderbook;
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> priceDist(1, 1000);
    std::uniform_int_distribution<int> qtyDist(1, 1000);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> typeDist(0, 1);

    const int delayMs = 5;

    for (int i = 0; i < 5000; ++i) {
        Side side = static_cast<Side>(sideDist(rng));
        OrderType type = static_cast<OrderType>(typeDist(rng));
        Price price = priceDist(rng);
        Quantity qty = qtyDist(rng);
        OrderId id = GenerateOrderId();

        auto order = std::make_shared<Order>(type, id, side, price, qty);

        std::cout << "Order Placed: ID=" << id
                  << " Type=" << (type == OrderType::GoodTillCancel ? "GTC" : "FAK")
                  << " Side=" << (side == Side::Buy ? "Buy" : "Sell")
                  << " Price=" << price
                  << " Quantity=" << qty << '\n';

        auto trades = orderbook.AddOrder(order);
        for (const auto& trade : trades) {
            std::cout << "Trade Executed: Buy ID=" << trade.GetBid().id
                      << " Sell ID=" << trade.GetAsk().id
                      << " Price=" << trade.GetBid().price
                      << " Quantity=" << trade.GetBid().qty << '\n';
        }

        OrderbookPrinter::Print(orderbook.GetOrderInfos());
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    std::cout << "\nFinal Orderbook State:\nBids:\n";
    for (const auto& level : orderbook.GetOrderInfos().GetBids())
        std::cout << "  Price: " << level.price << ", Quantity: " << level.quantity << '\n';

    std::cout << "Asks:\n";
    for (const auto& level : orderbook.GetOrderInfos().GetAsks())
        std::cout << "  Price: " << level.price << ", Quantity: " << level.quantity << '\n';

    return 0;
}
