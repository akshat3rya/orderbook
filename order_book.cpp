#include "order_book.hpp"
#include <iostream>
#include <iomanip>

OrderBook::~OrderBook()
{
    for (auto& [price, level] : bids_) delete level;
    for (auto& [price, level] : asks_) delete level;
    for (auto& [id, order] : orderIndex_) delete order;
}

PriceLevel* OrderBook::getOrCreateLevel(Side side, int64_t price)
{
    if (side == Side::BUY)
    {
        auto it = bids_.find(price);
        if (it != bids_.end()) return it->second;
        auto* lvl = new PriceLevel(price);
        bids_.emplace(price, lvl);
        return lvl;
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) return it->second;
        auto* lvl = new PriceLevel(price);
        asks_.emplace(price, lvl);
        return lvl;
    }
}

void OrderBook::removeLevelIfEmpty(Side side, PriceLevel* level)
{
    if (!level->empty()) return;
    if (side == Side::BUY) bids_.erase(level->price);
    else asks_.erase(level->price);
    delete level;
}

bool OrderBook::insert(uint64_t id, Side side, int64_t price, int64_t qty)
{
    if (orderIndex_.count(id)) return false;
    if (qty <= 0) return false;
    auto* order = new Order{};
    order->id = id;
    order->side = side;
    order->price = price;
    order->qty = qty;
    order->seq = seqCounter_++;
    PriceLevel* level = getOrCreateLevel(side, price);
    level->pushBack(order); 
    orderIndex_.emplace(id, order);
    return true;
}

bool OrderBook::cancel(uint64_t id)
{
    auto it = orderIndex_.find(id);
    if (it == orderIndex_.end()) return false;
    Order* order = it->second;
    PriceLevel* level = order->level;
    Side side = order->side;

    level->unlink(order);       
    removeLevelIfEmpty(side, level);

    orderIndex_.erase(it);
    delete order;
    return true;
}

ModifyResult OrderBook::modify(uint64_t id, int64_t newQty)
{
    auto it = orderIndex_.find(id);
    if (it == orderIndex_.end()) 
        return ModifyResult::NOT_FOUND;

    Order* order = it->second;

    if (newQty <= 0)
    {
        cancel(id);
        return ModifyResult::RESULTED_IN_CANCEL;
    }
    if (newQty >= order->qty) return ModifyResult::QTY_MUST_DECREASE;

    int64_t delta = order->qty - newQty;
    order->qty = newQty;
    order->level->totalQty -= delta;

    return ModifyResult::OK;
}

std::optional<int64_t> OrderBook::bestBid() const 
{
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> OrderBook::bestAsk() const 
{
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

void OrderBook::print(int depth) const 
{
    std::cout << std::fixed;
    std::cout << "----- BIDS -----        ----- ASKS -----\n";

    auto bIt = bids_.begin();
    auto aIt = asks_.begin();
    for (int i = 0; i < depth && (bIt != bids_.end() || aIt != asks_.end()); ++i)
    {  
        std::string bidStr = "";
        std::string askStr = "";

        if (bIt != bids_.end())
        {
            bidStr = std::to_string(bIt->second->price) + " x " + std::to_string(bIt->second->totalQty) + " (" + std::to_string(bIt->second->orderCount) + ")";
            ++bIt;
        }
        if (aIt != asks_.end())
        {
            askStr = std::to_string(aIt->second->price) + " x " + std::to_string(aIt->second->totalQty) + " (" + std::to_string(aIt->second->orderCount) + ")";
            ++aIt;
        }

        std::cout << std::left << std::setw(24) << bidStr << std::setw(24) << askStr << "\n";
    }
    if (bids_.empty() && asks_.empty()) 
        std::cout << "(empty book)\n";
}